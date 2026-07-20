#include "neverc/Plugin/Host/PluginABILowering.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/MathExtras.h"
#include <cstddef>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

Error classificationError(StringRef ABIName, const Twine &Detail) {
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

Error validateCoercion(StringRef ABIName, StringRef Label,
                       NevercABICoercionKind Kind, uint32_t BitWidth,
                       uint32_t AddressSpace) {
  if (Kind > NEVERC_ABI_COERCE_POINTER)
    return classificationError(ABIName, Label + " has an unknown kind");
  if (Kind == NEVERC_ABI_COERCE_NONE) {
    if (BitWidth != 0 || AddressSpace != 0)
      return classificationError(
          ABIName, Label + " carries data without a type");
    return Error::success();
  }
  if (BitWidth == 0)
    return classificationError(ABIName, Label + " has no bit width");
  if (Kind != NEVERC_ABI_COERCE_POINTER && AddressSpace != 0)
    return classificationError(
        ABIName, Label + " has an address space for a non-pointer type");
  if (Kind == NEVERC_ABI_COERCE_FLOAT && BitWidth != 16 &&
      BitWidth != 32 && BitWidth != 64 && BitWidth != 80 &&
      BitWidth != 128)
    return classificationError(
        ABIName, Label + " has an unsupported floating width");
  return Error::success();
}

Error validateCoerceAndExpandElements(
    StringRef ABIName, const NevercABITypeDescriptor &Type,
    const NevercABIArgumentClassification &Classification) {
  const NevercStructArrayView Elements =
      Classification.CoerceAndExpandElements;
  if (Elements.Count == 0 || Elements.Count > 64 || !Elements.Data ||
      Elements.ElementStride < sizeof(NevercABICoercionElement))
    return classificationError(
        ABIName, "coerce-and-expand has an invalid element array");
  if (Classification.CoerceAndExpandSize == 0 ||
      static_cast<uint64_t>(Classification.CoerceAndExpandSize) * 8 !=
          Type.BitWidth)
    return classificationError(
        ABIName, "coerce-and-expand size does not match the source type");

  uint64_t PreviousEnd = 0;
  const auto *Bytes = static_cast<const uint8_t *>(Elements.Data);
  for (uint64_t I = 0; I != Elements.Count; ++I) {
    const auto *Element =
        reinterpret_cast<const NevercABICoercionElement *>(
            Bytes + I * Elements.ElementStride);
    if (!validHeader(Element->Header, sizeof(*Element)) ||
        Element->Reserved != 0)
      return classificationError(
          ABIName, "coerce-and-expand element has a bad ABI record");
    if (Error E = validateCoercion(
            ABIName, "coerce-and-expand element",
            Element->Coercion, Element->BitWidth,
            Element->AddressSpace))
      return E;
    if (Element->Coercion == NEVERC_ABI_COERCE_NONE ||
        Element->BitWidth % 8 != 0)
      return classificationError(
          ABIName,
          "coerce-and-expand element is not a byte-sized scalar");
    const uint64_t ElementEnd =
        static_cast<uint64_t>(Element->Offset) +
        Element->BitWidth / 8;
    if (Element->Offset < PreviousEnd ||
        ElementEnd > Classification.CoerceAndExpandSize)
      return classificationError(
          ABIName, "coerce-and-expand elements overlap or exceed the type");
    PreviousEnd = ElementEnd;
  }
  return Error::success();
}

Error validateClassification(
    StringRef ABIName, const NevercABITypeDescriptor &Type,
    const NevercABIArgumentClassification &Classification) {
  constexpr size_t Required =
      offsetof(NevercABIArgumentClassification, Reserved) +
      sizeof(NevercABIArgumentClassification::Reserved);
  constexpr NevercABIArgumentFlags KnownFlags =
      NEVERC_ABI_ARGUMENT_BYVAL | NEVERC_ABI_ARGUMENT_REALIGN |
      NEVERC_ABI_ARGUMENT_INREG |
      NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
      NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED |
      NEVERC_ABI_ARGUMENT_SIGN_EXTEND |
      NEVERC_ABI_ARGUMENT_PADDING_INREG;
  if (!validHeader(Classification.Header, Required))
    return classificationError(ABIName, "bad ABI table header");
  if (Classification.Reserved[0] != 0 ||
      Classification.Reserved[1] != 0)
    return classificationError(ABIName, "nonzero reserved fields");
  if (Classification.Flags & ~KnownFlags)
    return classificationError(ABIName, "unknown argument flags");
  if (Error E = validateCoercion(
          ABIName, "coercion", Classification.Coercion,
          Classification.CoercionBitWidth,
          Classification.Coercion == NEVERC_ABI_COERCE_POINTER
              ? Classification.AddressSpace
              : 0))
    return E;
  if (Classification.AddressSpace != 0 &&
      Classification.Coercion != NEVERC_ABI_COERCE_POINTER &&
      Classification.Kind != NEVERC_ABI_ARGUMENT_INDIRECT_ALIASED)
    return classificationError(
        ABIName, "argument has an address space without a pointer role");
  if (Error E = validateCoercion(
          ABIName, "padding coercion",
          Classification.PaddingCoercion,
          Classification.PaddingBitWidth,
          Classification.PaddingAddressSpace))
    return E;
  const bool HasPadding =
      Classification.PaddingCoercion != NEVERC_ABI_COERCE_NONE;
  if (!HasPadding &&
      (Classification.Flags & NEVERC_ABI_ARGUMENT_PADDING_INREG))
    return classificationError(
        ABIName, "padding-in-register is set without padding");
  if (Classification.Alignment != 0 &&
      !isPowerOf2_32(Classification.Alignment))
    return classificationError(ABIName,
                               "alignment is not a power of two");
  const bool HasCoerceAndExpandElements =
      Classification.CoerceAndExpandElements.Count != 0 ||
      Classification.CoerceAndExpandElements.Data != nullptr ||
      Classification.CoerceAndExpandElements.ElementStride != 0 ||
      Classification.CoerceAndExpandSize != 0;
  if (Classification.Kind !=
          NEVERC_ABI_ARGUMENT_COERCE_AND_EXPAND &&
      HasCoerceAndExpandElements)
    return classificationError(
        ABIName, "non-coerce-and-expand argument carries element data");

  switch (Classification.Kind) {
  case NEVERC_ABI_ARGUMENT_DIRECT:
    if (Classification.Flags &
        (NEVERC_ABI_ARGUMENT_BYVAL |
         NEVERC_ABI_ARGUMENT_REALIGN |
         NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
         NEVERC_ABI_ARGUMENT_SIGN_EXTEND))
      return classificationError(ABIName,
                                 "direct argument has indirect flags");
    if (Classification.Coercion == NEVERC_ABI_COERCE_NONE &&
        Classification.DirectOffset != 0)
      return classificationError(
          ABIName, "direct offset requires a coercion type");
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE &&
        Classification.DirectOffset != 0) {
      const uint64_t EndBit =
          static_cast<uint64_t>(Classification.DirectOffset) * 8 +
          Classification.CoercionBitWidth;
      if (EndBit > Type.BitWidth)
        return classificationError(
            ABIName, "direct coercion exceeds the source type");
    }
    if (Type.Kind == NEVERC_ABI_TYPE_POINTER &&
        Classification.Coercion == NEVERC_ABI_COERCE_POINTER &&
        (Classification.AddressSpace != Type.AddressSpace ||
         Classification.CoercionBitWidth != Type.BitWidth))
      return classificationError(
          ABIName, "pointer coercion is incompatible with the source type");
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
    if (Classification.DirectOffset != 0 ||
        Classification.Alignment != 0)
      return classificationError(
          ABIName, "extend argument has direct layout data");
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
  case NEVERC_ABI_ARGUMENT_INDIRECT_ALIASED:
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        Classification.DirectOffset != 0)
      return classificationError(
          ABIName, "indirect-aliased argument cannot request coercion");
    if (Classification.Alignment == 0)
      return classificationError(
          ABIName, "indirect-aliased argument has no alignment");
    if (Classification.Flags &
        (NEVERC_ABI_ARGUMENT_BYVAL |
         NEVERC_ABI_ARGUMENT_INREG |
         NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
         NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED |
         NEVERC_ABI_ARGUMENT_SIGN_EXTEND))
      return classificationError(
          ABIName, "indirect-aliased argument has invalid flags");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_IGNORE:
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        HasPadding ||
        Classification.Alignment != 0 ||
        Classification.AddressSpace != 0 ||
        Classification.DirectOffset != 0 ||
        Classification.Flags != 0 ||
        HasCoerceAndExpandElements)
      return classificationError(
          ABIName, "ignored argument carries lowering data");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_EXPAND:
    if (!(Type.Flags & NEVERC_ABI_TYPE_AGGREGATE))
      return classificationError(
          ABIName, "expand classification requires an aggregate");
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        Classification.Alignment != 0 ||
        Classification.AddressSpace != 0 ||
        Classification.DirectOffset != 0 ||
        (Classification.Flags &
         ~NEVERC_ABI_ARGUMENT_PADDING_INREG))
      return classificationError(ABIName,
                                 "expanded argument carries lowering data");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_COERCE_AND_EXPAND:
    if (!(Type.Flags & NEVERC_ABI_TYPE_AGGREGATE))
      return classificationError(
          ABIName,
          "coerce-and-expand classification requires an aggregate");
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        HasPadding || Classification.Alignment != 0 ||
        Classification.AddressSpace != 0 ||
        Classification.DirectOffset != 0 ||
        Classification.Flags != 0)
      return classificationError(
          ABIName, "coerce-and-expand carries incompatible lowering data");
    return validateCoerceAndExpandElements(
        ABIName, Type, Classification);
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
      CallingConvention && CallingConvention->PlanCallingConvention
          ? llvm::CallingConv::NeverC_Custom
          : CallingConvention
                ? CallingConvention->LLVMCallingConvention
                : 0;

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
