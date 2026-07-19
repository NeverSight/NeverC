#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error descriptorError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool validHeader(const NevercABITableHeader &Header, uint64_t Required) {
  return Header.StructSize >= Required &&
         Header.Major == NEVERC_TARGET_API_MAJOR &&
         Header.Minor <= NEVERC_TARGET_API_MINOR && Header.Flags == 0;
}

Expected<std::string> copyString(NevercStringView View, StringRef Field,
                                 bool AllowEmpty) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return descriptorError(Field + " has an invalid string view");
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0'))
    return descriptorError(Field + " has an invalid value");
  return Text.str();
}

Expected<std::vector<std::string>>
copyStrings(NevercStringArrayView View, StringRef Field) {
  if (View.Count > 4096 ||
      (View.Count != 0 &&
       (!View.Data || View.ElementStride < sizeof(NevercStringView) ||
        View.ElementStride > std::numeric_limits<size_t>::max() ||
        View.ElementStride >
            std::numeric_limits<size_t>::max() / View.Count)))
    return descriptorError(Field + " has an invalid array view");
  std::vector<std::string> Result;
  Result.reserve(static_cast<size_t>(View.Count));
  const auto *Bytes = reinterpret_cast<const uint8_t *>(View.Data);
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercStringView *>(
        Bytes + static_cast<size_t>(I * View.ElementStride));
    auto Value = copyString(*Item, Field, false);
    if (!Value)
      return Value.takeError();
    Result.push_back(std::move(*Value));
  }
  return Result;
}

Expected<std::vector<NevercInterfaceID>>
copyIDs(NevercInterfaceIDArrayView View, StringRef Field) {
  if (View.Count > 4096 ||
      (View.Count != 0 &&
       (!View.Data || View.ElementStride < sizeof(NevercInterfaceID) ||
        View.ElementStride > std::numeric_limits<size_t>::max() ||
        View.ElementStride >
            std::numeric_limits<size_t>::max() / View.Count)))
    return descriptorError(Field + " has an invalid array view");
  std::vector<NevercInterfaceID> Result;
  Result.reserve(static_cast<size_t>(View.Count));
  const auto *Bytes = reinterpret_cast<const uint8_t *>(View.Data);
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercInterfaceID *>(
        Bytes + static_cast<size_t>(I * View.ElementStride));
    if (Item->High == 0 && Item->Low == 0)
      return descriptorError(Field + " contains a zero ID");
    Result.push_back(*Item);
  }
  return Result;
}

bool sortedUnique(ArrayRef<std::string> Values) {
  return std::adjacent_find(Values.begin(), Values.end(),
                            std::greater_equal<std::string>()) ==
         Values.end();
}

bool sortedUniqueIDs(ArrayRef<NevercInterfaceID> Values) {
  for (size_t I = 1; I < Values.size(); ++I) {
    const NevercInterfaceID Left = Values[I - 1];
    const NevercInterfaceID Right = Values[I];
    if (Left.High > Right.High ||
        (Left.High == Right.High && Left.Low >= Right.Low))
      return false;
  }
  return true;
}

bool powerOfTwoWidth(uint32_t Value) {
  return Value != 0 && Value <= 4096 && (Value % 8) == 0 &&
         (Value & (Value - 1)) == 0;
}

uint64_t modelBit(uint32_t Value) {
  return Value == 0 || Value > 64 ? 0 : UINT64_C(1) << (Value - 1);
}

bool validDigest(StringRef Value) {
  return Value.size() == 64 &&
         std::all_of(Value.begin(), Value.end(), [](char C) {
           return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
         });
}

bool hasStackAlignmentToken(StringRef Layout) {
  while (!Layout.empty()) {
    auto Part = Layout.split('-');
    if (Part.first.size() > 1 && Part.first.front() == 'S')
      return true;
    Layout = Part.second;
  }
  return false;
}

} // namespace

Expected<VerifiedTargetMachineDescriptor>
verifyTargetMachineDescriptor(
    const NevercTargetMachineDescriptor &Descriptor) {
  constexpr uint64_t Required =
      offsetof(NevercTargetMachineDescriptor, Reserved) +
      sizeof(NevercTargetMachineDescriptor::Reserved);
  constexpr NevercTargetExecutionLevel KnownExecutionLevels =
      NEVERC_TARGET_EXECUTION_USER | NEVERC_TARGET_EXECUTION_KERNEL |
      NEVERC_TARGET_EXECUTION_HYPERVISOR |
      NEVERC_TARGET_EXECUTION_FIRMWARE;
  constexpr uint64_t KnownRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC |
      NEVERC_TARGET_RELOCATION_MASK_PIC |
      NEVERC_TARGET_RELOCATION_MASK_DYNAMIC_NO_PIC |
      NEVERC_TARGET_RELOCATION_MASK_ROPI |
      NEVERC_TARGET_RELOCATION_MASK_RWPI |
      NEVERC_TARGET_RELOCATION_MASK_ROPI_RWPI;
  constexpr uint64_t KnownCodeModels =
      NEVERC_TARGET_CODE_MODEL_MASK_TINY |
      NEVERC_TARGET_CODE_MODEL_MASK_SMALL |
      NEVERC_TARGET_CODE_MODEL_MASK_KERNEL |
      NEVERC_TARGET_CODE_MODEL_MASK_MEDIUM |
      NEVERC_TARGET_CODE_MODEL_MASK_LARGE;
  constexpr uint32_t MaximumTargetInfoWidth =
      std::numeric_limits<unsigned char>::max();
  if (Descriptor.PointerWidth > MaximumTargetInfoWidth ||
      Descriptor.IntWidth > MaximumTargetInfoWidth ||
      Descriptor.LongWidth > MaximumTargetInfoWidth ||
      Descriptor.LongLongWidth > MaximumTargetInfoWidth ||
      Descriptor.MaximumAtomicWidth > MaximumTargetInfoWidth)
    return descriptorError(
        "Target machine descriptor has an unsupported TargetInfo width");
  if (!validHeader(Descriptor.Header, Required) ||
      Descriptor.Reserved != 0 ||
      (Descriptor.Endianness != NEVERC_TARGET_ENDIAN_LITTLE &&
       Descriptor.Endianness != NEVERC_TARGET_ENDIAN_BIG) ||
      !powerOfTwoWidth(Descriptor.PointerWidth) ||
      !powerOfTwoWidth(Descriptor.IntWidth) ||
      !powerOfTwoWidth(Descriptor.LongWidth) ||
      !powerOfTwoWidth(Descriptor.LongLongWidth) ||
      !powerOfTwoWidth(Descriptor.StackAlignment) ||
      !powerOfTwoWidth(Descriptor.MaximumAtomicWidth) ||
      (Descriptor.MaximumVectorAlignment != 0 &&
       !powerOfTwoWidth(Descriptor.MaximumVectorAlignment)) ||
      Descriptor.BuiltinVaListKind <
          NEVERC_TARGET_VA_LIST_CHAR_POINTER ||
      Descriptor.BuiltinVaListKind >
          NEVERC_TARGET_VA_LIST_X86_64 ||
      Descriptor.ExecutionLevels == 0 ||
      (Descriptor.ExecutionLevels & ~KnownExecutionLevels) != 0 ||
      Descriptor.DefaultExecutionLevel == 0 ||
      (Descriptor.DefaultExecutionLevel &
       (Descriptor.DefaultExecutionLevel - 1)) != 0 ||
      (Descriptor.ExecutionLevels &
       Descriptor.DefaultExecutionLevel) == 0 ||
      (Descriptor.TLSSupported != NEVERC_FALSE &&
       Descriptor.TLSSupported != NEVERC_TRUE))
    return descriptorError("Target machine descriptor has invalid scalars");
  if (Descriptor.SupportedRelocationModels == 0 ||
      (Descriptor.SupportedRelocationModels & ~KnownRelocationModels) != 0 ||
      (Descriptor.SupportedRelocationModels &
       modelBit(Descriptor.DefaultRelocationModel)) == 0)
    return descriptorError(
        "Target default relocation model is not supported");
  if (Descriptor.SupportedCodeModels == 0 ||
      (Descriptor.SupportedCodeModels & ~KnownCodeModels) != 0 ||
      (Descriptor.SupportedCodeModels &
       modelBit(Descriptor.DefaultCodeModel)) == 0)
    return descriptorError("Target default code model is not supported");
  if (Descriptor.ExceptionModel < NEVERC_TARGET_EXCEPTION_NONE ||
      Descriptor.ExceptionModel > NEVERC_TARGET_EXCEPTION_WASM)
    return descriptorError("Target exception model is invalid");
  if (Descriptor.UnwindModel < NEVERC_TARGET_UNWIND_NONE ||
      Descriptor.UnwindModel > NEVERC_TARGET_UNWIND_WASM)
    return descriptorError("Target unwind model is invalid");

  VerifiedTargetMachineDescriptor Result;
  auto RawTriple = copyString(Descriptor.RawTriple, "raw triple", false);
  auto Architecture =
      copyString(Descriptor.Architecture, "architecture", false);
  auto Vendor = copyString(Descriptor.Vendor, "vendor", true);
  auto OperatingSystem =
      copyString(Descriptor.OperatingSystem, "operating system", true);
  auto Environment =
      copyString(Descriptor.Environment, "environment", true);
  auto DataLayoutText =
      copyString(Descriptor.DataLayout, "data layout", false);
  auto DefaultCPU =
      copyString(Descriptor.DefaultCPU, "default CPU", false);
  auto TuneCPU = copyString(Descriptor.TuneCPU, "tune CPU", true);
  auto GlobalLabelPrefix =
      copyString(Descriptor.GlobalLabelPrefix, "global label prefix", true);
  auto SchemaDigest =
      copyString(Descriptor.SchemaDigest, "schema digest", false);
  if (!RawTriple)
    return RawTriple.takeError();
  if (!Architecture)
    return Architecture.takeError();
  if (!Vendor)
    return Vendor.takeError();
  if (!OperatingSystem)
    return OperatingSystem.takeError();
  if (!Environment)
    return Environment.takeError();
  if (!DataLayoutText)
    return DataLayoutText.takeError();
  if (!DefaultCPU)
    return DefaultCPU.takeError();
  if (!TuneCPU)
    return TuneCPU.takeError();
  if (!GlobalLabelPrefix)
    return GlobalLabelPrefix.takeError();
  if (!SchemaDigest)
    return SchemaDigest.takeError();
  if (!validDigest(*SchemaDigest))
    return descriptorError("Target schema digest must be lowercase SHA-256");
  if (!hasStackAlignmentToken(*DataLayoutText))
    return descriptorError(
        "Target data layout must declare a stack alignment");

  Expected<DataLayout> Parsed = DataLayout::parse(*DataLayoutText);
  if (!Parsed)
    return joinErrors(descriptorError("Target data layout is invalid"),
                      Parsed.takeError());
  if (Parsed->getPointerSizeInBits(0) != Descriptor.PointerWidth)
    return descriptorError(
        "Target pointer width disagrees with data layout");
  const bool LittleEndian =
      Descriptor.Endianness == NEVERC_TARGET_ENDIAN_LITTLE;
  if (Parsed->isLittleEndian() != LittleEndian)
    return descriptorError(
        "Target endianness disagrees with data layout");
  const char Prefix = Parsed->getGlobalPrefix();
  const std::string ParsedPrefix =
      Prefix == '\0' ? std::string() : std::string(1, Prefix);
  if (ParsedPrefix != *GlobalLabelPrefix)
    return descriptorError(
        "Target global label prefix disagrees with data layout");
  if (Parsed->getStackAlignment().value() * 8 !=
      Descriptor.StackAlignment)
    return descriptorError(
        "Target stack alignment disagrees with data layout");

  auto CPUs = copyStrings(Descriptor.CPUs, "CPU");
  auto ABIs = copyIDs(Descriptor.ABIs, "target ABI");
  auto CallingConventions =
      copyIDs(Descriptor.CallingConventions, "calling convention");
  auto ObjectFormats = copyIDs(Descriptor.ObjectFormats, "object Format");
  if (!CPUs)
    return CPUs.takeError();
  if (!ABIs)
    return ABIs.takeError();
  if (!CallingConventions)
    return CallingConventions.takeError();
  if (!ObjectFormats)
    return ObjectFormats.takeError();
  if (!sortedUnique(*CPUs))
    return descriptorError("Target CPU list must be sorted and unique");
  if (!CPUs->empty() &&
      !std::binary_search(CPUs->begin(), CPUs->end(), *DefaultCPU))
    return descriptorError("Target default CPU is absent from CPU list");
  if (!TuneCPU->empty() && !CPUs->empty() &&
      !std::binary_search(CPUs->begin(), CPUs->end(), *TuneCPU))
    return descriptorError("Target tune CPU is absent from CPU list");
  if (!sortedUniqueIDs(*ObjectFormats))
    return descriptorError(
        "Target object Format IDs must be sorted and unique");
  if (!sortedUniqueIDs(*ABIs))
    return descriptorError(
        "Target ABI IDs must be sorted and unique");
  if (!sortedUniqueIDs(*CallingConventions))
    return descriptorError(
        "Target calling convention IDs must be sorted and unique");

  constexpr uint64_t RequiredFeature =
      offsetof(NevercTargetFeatureDescriptor, Reserved) +
      sizeof(NevercTargetFeatureDescriptor::Reserved);
  if (Descriptor.Features.Count > 4096 ||
      (Descriptor.Features.Count != 0 &&
       (!Descriptor.Features.Data ||
        Descriptor.Features.ElementStride < RequiredFeature ||
        Descriptor.Features.ElementStride >
            std::numeric_limits<size_t>::max() ||
        Descriptor.Features.ElementStride >
            std::numeric_limits<size_t>::max() /
                Descriptor.Features.Count)))
    return descriptorError("Target feature array is invalid");
  const auto *FeatureBytes =
      static_cast<const uint8_t *>(Descriptor.Features.Data);
  for (uint64_t I = 0; I != Descriptor.Features.Count; ++I) {
    const auto *Feature =
        reinterpret_cast<const NevercTargetFeatureDescriptor *>(
            FeatureBytes +
            static_cast<size_t>(I * Descriptor.Features.ElementStride));
    if (!validHeader(Feature->Header, RequiredFeature) ||
        Feature->Reserved != 0 ||
        (Feature->EnabledByDefault != NEVERC_FALSE &&
         Feature->EnabledByDefault != NEVERC_TRUE))
      return descriptorError("Target feature descriptor is invalid");
    auto Name = copyString(Feature->Name, "feature name", false);
    auto Implies = copyStrings(Feature->Implies, "feature implication");
    auto Conflicts = copyStrings(Feature->Conflicts, "feature conflict");
    if (!Name)
      return Name.takeError();
    if (!Implies)
      return Implies.takeError();
    if (!Conflicts)
      return Conflicts.takeError();
    if (!sortedUnique(*Implies) || !sortedUnique(*Conflicts))
      return descriptorError(
          "Target feature edges must be sorted and unique");
    Result.Features.push_back(
        {std::move(*Name), std::move(*Implies), std::move(*Conflicts),
         Feature->EnabledByDefault == NEVERC_TRUE});
  }
  for (size_t I = 1; I < Result.Features.size(); ++I)
    if (Result.Features[I - 1].Name >= Result.Features[I].Name)
      return descriptorError(
          "Target feature list must be sorted and unique");
  const auto FindFeature = [&](StringRef Name) {
    return std::lower_bound(
        Result.Features.begin(), Result.Features.end(), Name,
        [](const VerifiedTargetFeature &Feature, StringRef Value) {
          return Feature.Name < Value;
        });
  };
  for (const VerifiedTargetFeature &Feature : Result.Features) {
    for (const std::string &Implied : Feature.Implies)
      if (Implied == Feature.Name ||
          FindFeature(Implied) == Result.Features.end() ||
          FindFeature(Implied)->Name != Implied)
        return descriptorError(
            "Target feature implication references an unknown feature");
    for (const std::string &Conflict : Feature.Conflicts)
      if (Conflict == Feature.Name ||
          FindFeature(Conflict) == Result.Features.end() ||
          FindFeature(Conflict)->Name != Conflict)
        return descriptorError(
            "Target feature conflict references an unknown feature");
  }
  std::vector<uint8_t> Visit(Result.Features.size(), 0);
  std::function<bool(size_t)> HasCycle = [&](size_t Index) {
    if (Visit[Index] == 1)
      return true;
    if (Visit[Index] == 2)
      return false;
    Visit[Index] = 1;
    for (const std::string &Implied : Result.Features[Index].Implies) {
      size_t Next = static_cast<size_t>(
          FindFeature(Implied) - Result.Features.begin());
      if (HasCycle(Next))
        return true;
    }
    Visit[Index] = 2;
    return false;
  };
  for (size_t I = 0; I < Result.Features.size(); ++I)
    if (HasCycle(I))
      return descriptorError("Target feature implication graph has a cycle");

  constexpr uint64_t RequiredAddressSpace =
      offsetof(NevercTargetAddressSpaceDescriptor, Flags) +
      sizeof(NevercTargetAddressSpaceDescriptor::Flags);
  if (Descriptor.AddressSpaces.Count > 1024 ||
      (Descriptor.AddressSpaces.Count != 0 &&
       (!Descriptor.AddressSpaces.Data ||
        Descriptor.AddressSpaces.ElementStride < RequiredAddressSpace ||
        Descriptor.AddressSpaces.ElementStride >
            std::numeric_limits<size_t>::max() ||
        Descriptor.AddressSpaces.ElementStride >
            std::numeric_limits<size_t>::max() /
                Descriptor.AddressSpaces.Count)))
    return descriptorError("Target address-space array is invalid");
  const auto *AddressSpaceBytes =
      static_cast<const uint8_t *>(Descriptor.AddressSpaces.Data);
  uint32_t PreviousAddressSpace = 0;
  for (uint64_t I = 0; I != Descriptor.AddressSpaces.Count; ++I) {
    const auto *AddressSpace =
        reinterpret_cast<const NevercTargetAddressSpaceDescriptor *>(
            AddressSpaceBytes +
            static_cast<size_t>(I * Descriptor.AddressSpaces.ElementStride));
    if (!validHeader(AddressSpace->Header, RequiredAddressSpace) ||
        !powerOfTwoWidth(AddressSpace->PointerWidth) ||
        !powerOfTwoWidth(AddressSpace->ABIAlignment) ||
        !powerOfTwoWidth(AddressSpace->PreferredAlignment) ||
        AddressSpace->Flags != 0 ||
        (I != 0 && AddressSpace->AddressSpace <= PreviousAddressSpace))
      return descriptorError("Target address-space descriptor is invalid");
    if (Parsed->getPointerSizeInBits(AddressSpace->AddressSpace) !=
            AddressSpace->PointerWidth ||
        Parsed->getPointerABIAlignment(AddressSpace->AddressSpace).value() *
                8 !=
            AddressSpace->ABIAlignment ||
        Parsed->getPointerPrefAlignment(AddressSpace->AddressSpace).value() *
                8 !=
            AddressSpace->PreferredAlignment)
      return descriptorError(
          "Target address space disagrees with data layout");
    PreviousAddressSpace = AddressSpace->AddressSpace;
    Result.AddressSpaces.push_back(
        {AddressSpace->AddressSpace, AddressSpace->PointerWidth,
         AddressSpace->ABIAlignment, AddressSpace->PreferredAlignment,
         AddressSpace->Flags});
  }

  Result.RawTriple = std::move(*RawTriple);
  Result.Architecture = std::move(*Architecture);
  Result.Vendor = std::move(*Vendor);
  Result.OperatingSystem = std::move(*OperatingSystem);
  Result.Environment = std::move(*Environment);
  Result.DataLayout = std::move(*DataLayoutText);
  Result.DefaultCPU = std::move(*DefaultCPU);
  Result.TuneCPU = std::move(*TuneCPU);
  Result.GlobalLabelPrefix = std::move(*GlobalLabelPrefix);
  Result.SchemaDigest = std::move(*SchemaDigest);
  Result.CPUs = std::move(*CPUs);
  Result.ABIs = std::move(*ABIs);
  Result.CallingConventions = std::move(*CallingConventions);
  Result.ObjectFormats = std::move(*ObjectFormats);
  Result.SupportedRelocationModels =
      Descriptor.SupportedRelocationModels;
  Result.SupportedCodeModels = Descriptor.SupportedCodeModels;
  Result.DefaultRelocationModel = Descriptor.DefaultRelocationModel;
  Result.DefaultCodeModel = Descriptor.DefaultCodeModel;
  Result.ExceptionModel = Descriptor.ExceptionModel;
  Result.UnwindModel = Descriptor.UnwindModel;
  Result.Endianness = Descriptor.Endianness;
  Result.PointerWidth = Descriptor.PointerWidth;
  Result.IntWidth = Descriptor.IntWidth;
  Result.LongWidth = Descriptor.LongWidth;
  Result.LongLongWidth = Descriptor.LongLongWidth;
  Result.StackAlignment = Descriptor.StackAlignment;
  Result.MaximumAtomicWidth = Descriptor.MaximumAtomicWidth;
  Result.MaximumVectorAlignment =
      Descriptor.MaximumVectorAlignment;
  Result.BuiltinVaListKind = Descriptor.BuiltinVaListKind;
  Result.ExecutionLevels = Descriptor.ExecutionLevels;
  Result.DefaultExecutionLevel = Descriptor.DefaultExecutionLevel;
  Result.TLSSupported = Descriptor.TLSSupported == NEVERC_TRUE;
  return Result;
}

} // namespace neverc::plugin
