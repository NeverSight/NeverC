#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStringView stringView(const std::string &Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool validText(const std::string &Value, bool AllowEmpty) {
  return (AllowEmpty || !Value.empty()) &&
         Value.find('\0') == std::string::npos;
}

bool validDigest(const std::string &Value) {
  return Value.size() == 64 &&
         std::all_of(Value.begin(), Value.end(), [](char C) {
           return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
         });
}

} // namespace

NevercTargetKey OwnedTargetKey::view() const {
  FeatureViews.clear();
  FeatureViews.reserve(Features.size());
  for (const std::string &Feature : Features)
    FeatureViews.push_back(stringView(Feature));

  NevercTargetKey View{};
  View.Header = {sizeof(View), NEVERC_TARGET_API_MAJOR,
                 NEVERC_TARGET_API_MINOR, 0};
  View.TargetID = TargetID;
  View.RawTriple = stringView(RawTriple);
  View.Architecture = stringView(Architecture);
  View.Vendor = stringView(Vendor);
  View.OperatingSystem = stringView(OperatingSystem);
  View.Environment = stringView(Environment);
  View.CPU = stringView(CPU);
  View.TuneCPU = stringView(TuneCPU);
  View.Features = {FeatureViews.data(),
                   static_cast<uint64_t>(FeatureViews.size()),
                   sizeof(NevercStringView)};
  View.ABIID = ABIID;
  View.CallingConventionID = CallingConventionID;
  View.ObjectFormatID = ObjectFormatID;
  View.RelocationModel = RelocationModel;
  View.CodeModel = CodeModel;
  View.ExecutionLevel = ExecutionLevel;
  View.PointerWidth = PointerWidth;
  View.Endianness = Endianness;
  View.SchemaDigest = stringView(SchemaDigest);
  return View;
}

TargetKeyBuilder &TargetKeyBuilder::setTargetID(NevercTargetID ID) {
  Key.TargetID = ID;
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setTriple(std::string RawTripleValue,
                            std::string ArchitectureValue,
                            std::string VendorValue,
                            std::string OperatingSystemValue,
                            std::string EnvironmentValue) {
  Key.RawTriple = std::move(RawTripleValue);
  Key.Architecture = std::move(ArchitectureValue);
  Key.Vendor = std::move(VendorValue);
  Key.OperatingSystem = std::move(OperatingSystemValue);
  Key.Environment = std::move(EnvironmentValue);
  return *this;
}

TargetKeyBuilder &TargetKeyBuilder::setCPU(std::string CPUValue,
                                           std::string TuneCPUValue) {
  Key.CPU = std::move(CPUValue);
  Key.TuneCPU = std::move(TuneCPUValue);
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setFeatures(std::vector<std::string> FeatureValues) {
  Key.Features = std::move(FeatureValues);
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setFeatures(
    std::initializer_list<std::string> FeatureValues) {
  Key.Features.assign(FeatureValues.begin(), FeatureValues.end());
  return *this;
}

TargetKeyBuilder &TargetKeyBuilder::setABI(NevercTargetABIID ID) {
  Key.ABIID = ID;
  return *this;
}

TargetKeyBuilder &TargetKeyBuilder::setCallingConvention(
    NevercCallingConventionID ID) {
  Key.CallingConventionID = ID;
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setObjectFormat(NevercInterfaceID ID) {
  Key.ObjectFormatID = ID;
  return *this;
}

TargetKeyBuilder &TargetKeyBuilder::setCodeGeneration(
    NevercTargetRelocationModel RelocationModelValue,
    NevercTargetCodeModel CodeModelValue) {
  Key.RelocationModel = RelocationModelValue;
  Key.CodeModel = CodeModelValue;
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setExecution(NevercTargetExecutionLevel Level,
                               uint32_t PointerWidthValue,
                               NevercTargetEndianness EndiannessValue) {
  Key.ExecutionLevel = Level;
  Key.PointerWidth = PointerWidthValue;
  Key.Endianness = EndiannessValue;
  return *this;
}

TargetKeyBuilder &
TargetKeyBuilder::setSchemaDigest(std::string Digest) {
  Key.SchemaDigest = std::move(Digest);
  return *this;
}

Expected<OwnedTargetKey> TargetKeyBuilder::build() const {
  constexpr NevercTargetExecutionLevel KnownLevels =
      NEVERC_TARGET_EXECUTION_USER | NEVERC_TARGET_EXECUTION_KERNEL |
      NEVERC_TARGET_EXECUTION_HYPERVISOR |
      NEVERC_TARGET_EXECUTION_FIRMWARE;
  if (!nonzero(Key.TargetID) || !nonzero(Key.ABIID) ||
      !nonzero(Key.CallingConventionID) ||
      !nonzero(Key.ObjectFormatID))
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey contains a zero stable ID");
  if (!validText(Key.RawTriple, false) ||
      !validText(Key.Architecture, false) ||
      !validText(Key.Vendor, true) ||
      !validText(Key.OperatingSystem, true) ||
      !validText(Key.Environment, true) || !validText(Key.CPU, false) ||
      !validText(Key.TuneCPU, true))
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey contains an invalid string");
  if (Key.ExecutionLevel == 0 ||
      (Key.ExecutionLevel & ~KnownLevels) != 0 ||
      (Key.ExecutionLevel & (Key.ExecutionLevel - 1)) != 0)
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey execution level is invalid");
  if (Key.RelocationModel < NEVERC_TARGET_RELOCATION_STATIC ||
      Key.RelocationModel > NEVERC_TARGET_RELOCATION_ROPI_RWPI ||
      Key.CodeModel < NEVERC_TARGET_CODE_MODEL_TINY ||
      Key.CodeModel > NEVERC_TARGET_CODE_MODEL_LARGE)
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey code generation model is invalid");
  if (Key.PointerWidth == 0 || Key.PointerWidth > 1024 ||
      (Key.PointerWidth % 8) != 0)
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey pointer width is invalid");
  if (Key.Endianness != NEVERC_TARGET_ENDIAN_LITTLE &&
      Key.Endianness != NEVERC_TARGET_ENDIAN_BIG)
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey endianness is invalid");
  if (!validDigest(Key.SchemaDigest))
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey schema digest is invalid");

  OwnedTargetKey Result = Key;
  for (const std::string &Feature : Result.Features)
    if (!validText(Feature, false))
      return createStringError(inconvertibleErrorCode(),
                               "TargetKey feature is invalid");
  std::sort(Result.Features.begin(), Result.Features.end());
  if (std::adjacent_find(Result.Features.begin(), Result.Features.end()) !=
      Result.Features.end())
    return createStringError(inconvertibleErrorCode(),
                             "TargetKey features must be unique");
  return Result;
}

} // namespace neverc::plugin
