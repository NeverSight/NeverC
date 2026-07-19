#include "ABI/ABIInfoImpl.h"
#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/TypeEmitter.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Plugin/Host/PluginABILowering.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace neverc;
using namespace neverc::CodeGen;
using namespace neverc::Emit;
using namespace neverc::plugin;

namespace {

NevercABITypeDescriptor describeType(TreeContext &Context,
                                     QualType Type) {
  NevercABITypeDescriptor Result{};
  Result.Header = {sizeof(Result), NEVERC_TARGET_ABI_API_MAJOR,
                   NEVERC_TARGET_ABI_API_MINOR, 0};
  if (Type->isVoidType()) {
    Result.Kind = NEVERC_ABI_TYPE_VOID;
    return Result;
  }

  const TypeInfoChars Info = Context.getTypeInfoInChars(Type);
  Result.BitWidth =
      static_cast<uint32_t>(Info.Width.getQuantity() * 8);
  Result.Alignment =
      static_cast<uint32_t>(Info.Align.getQuantity());
  if (Type->isBooleanType())
    Result.Kind = NEVERC_ABI_TYPE_BOOLEAN;
  else if (Type->isEnumeralType())
    Result.Kind = NEVERC_ABI_TYPE_ENUM;
  else if (Type->isIntegerType())
    Result.Kind = NEVERC_ABI_TYPE_INTEGER;
  else if (Type->isRealFloatingType())
    Result.Kind = NEVERC_ABI_TYPE_FLOAT;
  else if (Type->isPointerType()) {
    Result.Kind = NEVERC_ABI_TYPE_POINTER;
    Result.AddressSpace = Context.getTargetAddressSpace(
        Type->getPointeeType().getAddressSpace());
  } else if (Type->isVectorType())
    Result.Kind = NEVERC_ABI_TYPE_VECTOR;
  else if (Type->isArrayType()) {
    Result.Kind = NEVERC_ABI_TYPE_ARRAY;
    Result.Flags |= NEVERC_ABI_TYPE_AGGREGATE;
  } else if (Type->isAnyComplexType())
    Result.Kind = NEVERC_ABI_TYPE_COMPLEX;
  else if (Type->getAsUnionType()) {
    Result.Kind = NEVERC_ABI_TYPE_UNION;
    Result.Flags |= NEVERC_ABI_TYPE_AGGREGATE;
  } else if (Type->isRecordType()) {
    Result.Kind = NEVERC_ABI_TYPE_RECORD;
    Result.Flags |= NEVERC_ABI_TYPE_AGGREGATE;
  } else
    Result.Kind = NEVERC_ABI_TYPE_OTHER;
  if (Type->hasSignedIntegerRepresentation())
    Result.Flags |= NEVERC_ABI_TYPE_SIGNED;
  return Result;
}

Expected<llvm::Type *>
coercionType(LLVMContext &Context,
             const NevercABIArgumentClassification &Classification) {
  switch (Classification.Coercion) {
  case NEVERC_ABI_COERCE_NONE:
    return nullptr;
  case NEVERC_ABI_COERCE_INTEGER:
    return IntegerType::get(Context,
                            Classification.CoercionBitWidth);
  case NEVERC_ABI_COERCE_POINTER:
    return llvm::PointerType::get(Context,
                                  Classification.AddressSpace);
  case NEVERC_ABI_COERCE_FLOAT:
    switch (Classification.CoercionBitWidth) {
    case 16:
      return llvm::Type::getHalfTy(Context);
    case 32:
      return llvm::Type::getFloatTy(Context);
    case 64:
      return llvm::Type::getDoubleTy(Context);
    case 80:
      return llvm::Type::getX86_FP80Ty(Context);
    case 128:
      return llvm::Type::getFP128Ty(Context);
    default:
      return createStringError(
          inconvertibleErrorCode(),
          "plugin ABI requested an unsupported floating coercion width");
    }
  default:
    llvm_unreachable("validated ABI coercion kind");
  }
}

Expected<ABIArgInfo>
convertClassification(
    LLVMContext &Context, QualType Type,
    const NevercABIArgumentClassification &Classification) {
  auto Coercion = coercionType(Context, Classification);
  if (!Coercion)
    return Coercion.takeError();
  ABIArgInfo Result;
  switch (Classification.Kind) {
  case NEVERC_ABI_ARGUMENT_DIRECT:
    Result = ABIArgInfo::getDirect(
        *Coercion, Classification.DirectOffset, nullptr,
        (Classification.Flags &
         NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED) != 0,
        Classification.Alignment);
    if (Classification.Flags & NEVERC_ABI_ARGUMENT_INREG)
      Result.setInReg(true);
    return Result;
  case NEVERC_ABI_ARGUMENT_EXTEND:
    Result = ABIArgInfo::getExtend(Type, *Coercion);
    Result.setSignExt(
        (Classification.Flags &
         NEVERC_ABI_ARGUMENT_SIGN_EXTEND) != 0);
    if (Classification.Flags & NEVERC_ABI_ARGUMENT_INREG)
      Result.setInReg(true);
    return Result;
  case NEVERC_ABI_ARGUMENT_INDIRECT:
    Result = ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(Classification.Alignment),
        (Classification.Flags & NEVERC_ABI_ARGUMENT_BYVAL) != 0,
        (Classification.Flags & NEVERC_ABI_ARGUMENT_REALIGN) != 0);
    if (Classification.Flags & NEVERC_ABI_ARGUMENT_INREG)
      Result.setInReg(true);
    Result.setSRetAfterThis(
        (Classification.Flags &
         NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS) != 0);
    return Result;
  case NEVERC_ABI_ARGUMENT_IGNORE:
    return ABIArgInfo::getIgnore();
  case NEVERC_ABI_ARGUMENT_EXPAND:
    return ABIArgInfo::getExpand();
  default:
    llvm_unreachable("validated ABI argument kind");
  }
}

class PluginABIInfo final : public DefaultABIInfo {
public:
  PluginABIInfo(TypeEmitter &Types,
                const PluginTargetInfo &PluginTarget)
      : DefaultABIInfo(Types), PluginTarget(PluginTarget) {}

  void computeInfo(ABIFunctionInfo &FI) const override {
    const auto *ABI = PluginTarget.abi();
    assert(ABI && ABI->ClassifyFunction);
    std::vector<NevercABITypeDescriptor> Parameters;
    Parameters.reserve(FI.arg_size());
    for (const auto &Argument : FI.arguments())
      Parameters.push_back(describeType(getContext(), Argument.type));
    const uint32_t RequiredArguments =
        FI.isVariadic()
            ? FI.getRequiredArgs().getNumRequiredArgs()
            : static_cast<uint32_t>(Parameters.size());
    PluginABILowering Lowering(
        *ABI, PluginTarget.callingConvention(),
        PluginTarget.task());
    auto Classification = Lowering.classify(
        describeType(getContext(), FI.getReturnType()), Parameters,
        FI.isVariadic(), RequiredArguments);
    if (!Classification) {
      reportError(Classification.takeError());
      DefaultABIInfo::computeInfo(FI);
      return;
    }

    auto ReturnInfo = convertClassification(
        getVMContext(), FI.getReturnType(),
        Classification->ReturnValue);
    if (!ReturnInfo) {
      reportError(ReturnInfo.takeError());
      DefaultABIInfo::computeInfo(FI);
      return;
    }
    std::vector<ABIArgInfo> ArgumentInfos;
    ArgumentInfos.reserve(FI.arg_size());
    for (size_t I = 0; I != FI.arg_size(); ++I) {
      auto ArgumentInfo = convertClassification(
          getVMContext(), FI.arguments()[I].type,
          Classification->Arguments[I]);
      if (!ArgumentInfo) {
        reportError(ArgumentInfo.takeError());
        DefaultABIInfo::computeInfo(FI);
        return;
      }
      ArgumentInfos.push_back(std::move(*ArgumentInfo));
    }
    FI.getReturnInfo() = std::move(*ReturnInfo);
    for (size_t I = 0; I != FI.arg_size(); ++I)
      FI.arguments()[I].info = std::move(ArgumentInfos[I]);
    FI.setEffectiveCallingConvention(
        Classification->LLVMCallingConvention);
  }

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Type) const override {
    const auto *ABI = PluginTarget.abi();
    const NevercABITypeDescriptor Parameter =
        describeType(getContext(), Type);
    PluginABILowering Lowering(
        *ABI, PluginTarget.callingConvention(),
        PluginTarget.task());
    auto Classification = Lowering.classify(
        describeType(getContext(), getContext().VoidTy),
        ArrayRef(Parameter), /*Variadic=*/true,
        /*RequiredArguments=*/0);
    if (!Classification) {
      reportError(Classification.takeError());
      return DefaultABIInfo::genVAArg(FE, VAListAddr, Type);
    }
    auto ArgumentInfo = convertClassification(
        getVMContext(), Type, Classification->Arguments.front());
    if (!ArgumentInfo) {
      reportError(ArgumentInfo.takeError());
      return DefaultABIInfo::genVAArg(FE, VAListAddr, Type);
    }

    if (ABI->VAArg.Kind == NEVERC_ABI_VA_ARG_VOID_POINTER)
      return emitVoidPtrVAArg(
          FE, VAListAddr, Type, ArgumentInfo->isIndirect(),
          getContext().getTypeInfoInChars(Type),
          CharUnits::fromQuantity(ABI->VAArg.SlotSize),
          ABI->VAArg.AllowHigherAlignment != NEVERC_FALSE);
    if ((ArgumentInfo->isDirect() || ArgumentInfo->isExtend()) &&
        (ArgumentInfo->getCoerceToType() ||
         ArgumentInfo->getInReg() ||
         ArgumentInfo->getDirectOffset())) {
      reportError(createStringError(
          inconvertibleErrorCode(),
          "LLVM va_arg lowering cannot represent the plugin ABI "
          "classification"));
      return DefaultABIInfo::genVAArg(FE, VAListAddr, Type);
    }
    return genVAArgInstr(FE, VAListAddr, Type, *ArgumentInfo);
  }

private:
  void reportError(Error ErrorValue) const {
    std::string Message =
        toString(std::move(ErrorValue)).str().str();
    DiagnosticsEngine &Diagnostics = getContext().getDiagnostics();
    const unsigned ID = Diagnostics.getCustomDiagID(
        DiagnosticsEngine::Error, "%0");
    Diagnostics.Report(ID) << Message;
  }

  const PluginTargetInfo &PluginTarget;
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
neverc::Emit::createPluginTargetCodeGenInfo(
    ModuleEmitter &ME, const PluginTargetInfo &PluginTarget) {
  return std::make_unique<TargetCodeGenInfo>(
      std::make_unique<PluginABIInfo>(ME.getTypes(), PluginTarget));
}
