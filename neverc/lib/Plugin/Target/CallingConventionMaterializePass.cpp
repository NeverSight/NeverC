#include "neverc/Plugin/Host/CallingConventionMaterialize.h"
#include "neverc/Plugin/Host/CallingConventionPlan.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/NeverCCallConv.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace ::neverc::plugin;
namespace ncp = ::neverc::plugin;

namespace {

bool isReservedRegisterName(StringRef Name) {
  return Name.equals_insensitive("rsp") ||
         Name.equals_insensitive("esp") ||
         Name.equals_insensitive("sp") ||
         Name.equals_insensitive("wsp") ||
         Name.equals_insensitive("x31") ||
         Name.equals_insensitive("w31");
}

MCRegister resolveRegister(const TargetRegisterInfo &TRI,
                           StringRef Name) {
  Name = Name.trim();
  if (Name.empty() || isReservedRegisterName(Name))
    return MCRegister();

  SmallString<16> Alternate;
  if (Name.size() > 1 &&
      (Name.front() == 'v' || Name.front() == 'V') &&
      llvm::all_of(Name.drop_front(), [](char C) {
        return std::isdigit(static_cast<unsigned char>(C));
      })) {
    Alternate.push_back('q');
    Alternate.append(Name.drop_front());
  }

  for (unsigned Register = 1; Register != TRI.getNumRegs();
       ++Register) {
    const MCRegister Candidate(Register);
    StringRef TableGenName = TRI.getName(Candidate);
    StringRef AssemblyName = TRI.getRegAsmName(Candidate);
    if (Name.equals_insensitive(TableGenName) ||
        Name.equals_insensitive(AssemblyName) ||
        (!Alternate.empty() &&
         (StringRef(Alternate).equals_insensitive(TableGenName) ||
          StringRef(Alternate).equals_insensitive(AssemblyName))))
      return Candidate;
  }
  return MCRegister();
}

bool isFPValue(Type *TypeValue) {
  return TypeValue->isFloatingPointTy() || TypeValue->isVectorTy();
}

bool isFPRegisterToken(StringRef Name) {
  Name = Name.trim();
  return Name.starts_with_insensitive("xmm") ||
         Name.starts_with_insensitive("ymm") ||
         Name.starts_with_insensitive("zmm") ||
         Name.starts_with_insensitive("v") ||
         Name.starts_with_insensitive("q") ||
         Name.starts_with_insensitive("d") ||
         Name.starts_with_insensitive("s") ||
         Name.starts_with_insensitive("h") ||
         Name.starts_with_insensitive("b");
}

Expected<uint32_t> fixedStoreSize(const DataLayout &Layout,
                                  Type *TypeValue) {
  TypeSize Size = Layout.getTypeStoreSize(TypeValue);
  if (Size.isScalable() ||
      Size.getFixedValue() >
          std::numeric_limits<uint32_t>::max())
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC custom calling convention cannot materialize a "
        "scalable or oversized value");
  return static_cast<uint32_t>(Size.getFixedValue());
}

Expected<NevercABITypeDescriptor>
describeIRType(const DataLayout &Layout, Type *TypeValue) {
  NevercABITypeDescriptor Result{};
  Result.Header = {sizeof(Result), NEVERC_TARGET_ABI_API_MAJOR,
                   NEVERC_TARGET_ABI_API_MINOR, 0};
  if (TypeValue->isVoidTy()) {
    Result.Kind = NEVERC_ABI_TYPE_VOID;
    return Result;
  }
  if (!TypeValue->isSized())
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC calling convention cannot describe an unsized IR type");

  TypeSize Width = Layout.getTypeSizeInBits(TypeValue);
  const uint64_t Alignment =
      Layout.getABITypeAlign(TypeValue).value();
  if (Width.isScalable() ||
      Width.getFixedValue() >
          std::numeric_limits<uint32_t>::max() ||
      Alignment > std::numeric_limits<uint32_t>::max())
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC calling convention cannot describe a scalable or "
        "oversized IR type");
  Result.BitWidth = static_cast<uint32_t>(Width.getFixedValue());
  Result.Alignment = static_cast<uint32_t>(Alignment);

  if (TypeValue->isIntegerTy(1))
    Result.Kind = NEVERC_ABI_TYPE_BOOLEAN;
  else if (TypeValue->isIntegerTy())
    Result.Kind = NEVERC_ABI_TYPE_INTEGER;
  else if (TypeValue->isFloatingPointTy())
    Result.Kind = NEVERC_ABI_TYPE_FLOAT;
  else if (auto *Pointer = dyn_cast<PointerType>(TypeValue)) {
    Result.Kind = NEVERC_ABI_TYPE_POINTER;
    Result.AddressSpace = Pointer->getAddressSpace();
  } else if (TypeValue->isVectorTy())
    Result.Kind = NEVERC_ABI_TYPE_VECTOR;
  else if (TypeValue->isArrayTy()) {
    Result.Kind = NEVERC_ABI_TYPE_ARRAY;
    Result.Flags |= NEVERC_ABI_TYPE_AGGREGATE;
  } else if (TypeValue->isStructTy()) {
    Result.Kind = NEVERC_ABI_TYPE_RECORD;
    Result.Flags |= NEVERC_ABI_TYPE_AGGREGATE;
  } else {
    Result.Kind = NEVERC_ABI_TYPE_OTHER;
  }
  return Result;
}

Expected<MaterializedCallingConventionPlan>
materializePluginPlan(
    FunctionType &FunctionTypeValue, const DataLayout &Layout,
    const PluginTargetSnapshot::NamedRecord &Convention,
    const PluginTargetSnapshot::TargetRecord &Target,
    PluginTaskContext &Task) {
  auto ReturnType =
      describeIRType(Layout, FunctionTypeValue.getReturnType());
  if (!ReturnType)
    return ReturnType.takeError();
  std::vector<NevercABITypeDescriptor> Parameters;
  Parameters.reserve(FunctionTypeValue.getNumParams());
  for (Type *ParameterType : FunctionTypeValue.params()) {
    auto Parameter = describeIRType(Layout, ParameterType);
    if (!Parameter)
      return Parameter.takeError();
    Parameters.push_back(*Parameter);
  }
  CallingConventionPlanner Planner(Convention, Target, &Task);
  return Planner.materialize(
      *ReturnType, Parameters, FunctionTypeValue.isVarArg(),
      static_cast<uint32_t>(Parameters.size()));
}

Error validateSerializedPlan(
    StringRef Text, StringRef SchemaDigest,
    const PluginTargetSnapshot::TargetRecord *Target,
    const PluginTargetSnapshot::NamedRecord *Convention) {
  llvm::neverc::CustomCCPlan Plan;
  if (!llvm::neverc::parseCustomCCPlan(Text, Plan))
    return createStringError(
        inconvertibleErrorCode(),
        "malformed NeverC calling convention plan");
  if (Plan.SchemaDigest != SchemaDigest)
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC calling convention plan belongs to a foreign target schema");
  if (Target &&
      (Plan.TargetIDHigh != Target->ID.High ||
       Plan.TargetIDLow != Target->ID.Low))
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC calling convention plan has a foreign target ID");
  if (Convention &&
      (Plan.CallingConventionIDHigh != Convention->ID.High ||
       Plan.CallingConventionIDLow != Convention->ID.Low))
    return createStringError(
        inconvertibleErrorCode(),
        "NeverC calling convention plan has a foreign convention ID");
  return Error::success();
}

CallingConventionPlanLocation registerLocation(
    uint32_t ValueIndex, uint32_t Size, uint32_t Alignment,
    MCRegister Register) {
  CallingConventionPlanLocation Location;
  Location.Kind = NEVERC_CC_LOCATION_REGISTER;
  Location.ValueIndex = ValueIndex;
  Location.Size = Size;
  Location.Alignment = Alignment;
  Location.RegisterNumber = Register.id();
  return Location;
}

CallingConventionPlanLocation stackLocation(
    uint32_t ValueIndex, uint32_t Size, uint32_t Alignment,
    uint32_t &NextStackOffset) {
  CallingConventionPlanLocation Location;
  Location.Kind = NEVERC_CC_LOCATION_STACK;
  Location.ValueIndex = ValueIndex;
  Location.Size = std::max<uint32_t>(Size, 8);
  Location.Alignment =
      std::max<uint32_t>(Alignment, Location.Size > 8 ? 16 : 8);
  NextStackOffset =
      alignTo(NextStackOffset, Location.Alignment);
  Location.StackOffset = NextStackOffset;
  NextStackOffset += Location.Size;
  return Location;
}

MCRegister takeRegister(
    const TargetRegisterInfo &TRI, ArrayRef<StringRef> Names,
    size_t &NextName, std::set<unsigned> &Used) {
  while (NextName != Names.size()) {
    MCRegister Register =
        resolveRegister(TRI, Names[NextName++]);
    if (Register.isValid() && Used.insert(Register.id()).second)
      return Register;
  }
  return MCRegister();
}

Expected<MaterializedCallingConventionPlan>
materializeLegacyPlan(Function &FunctionValue, TargetMachine &TM,
                      const llvm::neverc::CustomCCSpec &Spec) {
  const TargetSubtargetInfo *Subtarget =
      TM.getSubtargetImpl(FunctionValue);
  if (!Subtarget || !Subtarget->getRegisterInfo())
    return createStringError(
        inconvertibleErrorCode(),
        "target has no register information for NeverC custom "
        "calling convention materialization");
  const TargetRegisterInfo &TRI = *Subtarget->getRegisterInfo();
  const DataLayout &Layout = FunctionValue.getParent()->getDataLayout();

  MaterializedCallingConventionPlan Plan;
  Plan.SchemaDigest = "llvm-" + TM.getTargetTriple().str();
  Plan.StackAlignment =
      static_cast<uint32_t>(Layout.getStackAlignment().value());

  std::set<unsigned> UsedArgumentRegisters;
  size_t NextGPR = 0;
  size_t NextFP = 0;
  uint32_t NextStackOffset = 0;
  uint32_t ValueIndex = 0;
  for (Argument &ArgumentValue : FunctionValue.args()) {
    Type *TypeValue = ArgumentValue.getType();
    auto Size = fixedStoreSize(Layout, TypeValue);
    if (!Size)
      return Size.takeError();
    const uint32_t Alignment =
        static_cast<uint32_t>(
            Layout.getABITypeAlign(TypeValue).value());

    MCRegister Register;
    if (Spec.hasPositionalArgs()) {
      if (ValueIndex < Spec.Args.size()) {
        StringRef Token = Spec.Args[ValueIndex].trim();
        if (!llvm::neverc::isStackToken(Token) &&
            isFPRegisterToken(Token) == isFPValue(TypeValue)) {
          Register = resolveRegister(TRI, Token);
          if (Register.isValid() &&
              !UsedArgumentRegisters.insert(Register.id()).second)
            Register = MCRegister();
        }
      }
    } else if (isFPValue(TypeValue)) {
      Register = takeRegister(TRI, Spec.ArgXMM, NextFP,
                              UsedArgumentRegisters);
    } else {
      Register = takeRegister(TRI, Spec.ArgGPR, NextGPR,
                              UsedArgumentRegisters);
    }

    if (Register.isValid())
      Plan.ArgumentLocations.push_back(
          registerLocation(ValueIndex, *Size, Alignment, Register));
    else
      Plan.ArgumentLocations.push_back(
          stackLocation(ValueIndex, *Size, Alignment,
                        NextStackOffset));
    ++ValueIndex;
  }

  Type *ReturnType = FunctionValue.getReturnType();
  if (!ReturnType->isVoidTy()) {
    auto Size = fixedStoreSize(Layout, ReturnType);
    if (!Size)
      return Size.takeError();
    const uint32_t Alignment =
        static_cast<uint32_t>(
            Layout.getABITypeAlign(ReturnType).value());
    ArrayRef<StringRef> Names =
        isFPValue(ReturnType) ? ArrayRef<StringRef>(Spec.RetXMM)
                              : ArrayRef<StringRef>(Spec.RetGPR);
    if (!Names.empty()) {
      MCRegister Register = resolveRegister(TRI, Names.front());
      if (Register.isValid())
        Plan.ReturnLocations.push_back(
            registerLocation(0, *Size, Alignment, Register));
    }
  }

  std::set<unsigned> SeenCalleeSaved;
  for (StringRef Name : Spec.CalleeSaved) {
    MCRegister Register = resolveRegister(TRI, Name);
    if (Register.isValid() &&
        SeenCalleeSaved.insert(Register.id()).second)
      Plan.CalleeSavedRegisters.push_back(Register.id());
  }
  return Plan;
}

} // namespace

Error ncp::materializeCallingConventionPlans(
    Module &ModuleValue, TargetMachine &TM, PluginTaskContext *Task) {
  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  const PluginTargetSnapshot::TargetRecord *PluginTarget = nullptr;
  const PluginTargetSnapshot::NamedRecord *PluginConvention = nullptr;
  if (Task) {
    Snapshot = findPluginTargetSnapshot(
        Task->processServices(), Task->session().handle());
    if (Snapshot) {
      PluginTarget = Snapshot->selectedTarget();
      if (!PluginTarget)
        PluginTarget =
            Snapshot->matchTarget(ModuleValue.getTargetTriple());
      if (PluginTarget)
        PluginConvention = Snapshot->findCallingConvention(
            PluginTarget->DefaultCallingConvention);
      if (PluginConvention &&
          !PluginConvention->PlanCallingConvention)
        PluginConvention = nullptr;
    }
  }
  const std::string BuiltinSchemaDigest =
      "llvm-" + TM.getTargetTriple().str();

  for (Function &FunctionValue : ModuleValue) {
    if (FunctionValue.getCallingConv() !=
        CallingConv::NeverC_Custom)
      continue;

    if (FunctionValue.hasFnAttribute(
            llvm::neverc::CallConvPlanAttrName)) {
      StringRef PlanText =
          FunctionValue
              .getFnAttribute(llvm::neverc::CallConvPlanAttrName)
              .getValueAsString();
      if (Error E = validateSerializedPlan(
              PlanText,
              PluginConvention ? PluginTarget->Machine.SchemaDigest
                               : StringRef(BuiltinSchemaDigest),
              PluginConvention ? PluginTarget : nullptr,
              PluginConvention))
        return E;
      continue;
    }

    if (FunctionValue.hasFnAttribute(
            llvm::neverc::CallConvAttrName)) {
      StringRef LegacySpec =
          FunctionValue
              .getFnAttribute(llvm::neverc::CallConvAttrName)
              .getValueAsString();
      llvm::neverc::CustomCCSpec Spec;
      llvm::neverc::parseCustomCCSpec(LegacySpec, Spec);
      if (Spec.empty())
        return createStringError(
            inconvertibleErrorCode(),
            "legacy NeverC calling convention spec is empty or malformed");
      auto Plan = materializeLegacyPlan(FunctionValue, TM, Spec);
      if (!Plan)
        return Plan.takeError();
      FunctionValue.addFnAttr(
          llvm::neverc::CallConvPlanAttrName, Plan->serialize());
      FunctionValue.removeFnAttr(
          llvm::neverc::CallConvAttrName);
      continue;
    }

    if (!PluginConvention || !PluginTarget || !Task)
      return createStringError(
          inconvertibleErrorCode(),
          "NeverC custom calling convention has no materialization "
          "capability");
    auto Plan = materializePluginPlan(
        *FunctionValue.getFunctionType(), ModuleValue.getDataLayout(),
        *PluginConvention, *PluginTarget, *Task);
    if (!Plan)
      return Plan.takeError();
    FunctionValue.addFnAttr(
        llvm::neverc::CallConvPlanAttrName, Plan->serialize());
  }

  for (Function &FunctionValue : ModuleValue) {
    for (BasicBlock &Block : FunctionValue) {
      for (Instruction &InstructionValue : Block) {
        auto *Call = dyn_cast<CallBase>(&InstructionValue);
        if (!Call ||
            Call->getCallingConv() != CallingConv::NeverC_Custom)
          continue;

        if (Call->hasFnAttr(llvm::neverc::CallConvPlanAttrName))
          continue;
        if (Function *Callee = Call->getCalledFunction()) {
          if (Callee->hasFnAttribute(
                  llvm::neverc::CallConvPlanAttrName)) {
            Call->addFnAttr(Attribute::get(
                ModuleValue.getContext(),
                llvm::neverc::CallConvPlanAttrName,
                Callee
                    ->getFnAttribute(
                        llvm::neverc::CallConvPlanAttrName)
                    .getValueAsString()));
            continue;
          }
        }
        if (!PluginConvention || !PluginTarget || !Task)
          continue;
        auto Plan = materializePluginPlan(
            *Call->getFunctionType(), ModuleValue.getDataLayout(),
            *PluginConvention, *PluginTarget, *Task);
        if (!Plan)
          return Plan.takeError();
        Call->addFnAttr(Attribute::get(
            ModuleValue.getContext(),
            llvm::neverc::CallConvPlanAttrName, Plan->serialize()));
      }
    }
  }
  return Error::success();
}
