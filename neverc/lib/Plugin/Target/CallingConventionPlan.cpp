#include "neverc/Plugin/Host/CallingConventionPlan.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <utility>

using namespace llvm;
using namespace neverc::plugin;

namespace {

Error planError(StringRef Name, const Twine &Detail) {
  return createStringError(
      inconvertibleErrorCode(),
      "target calling convention '" + Name +
          "' produced an invalid plan: " + Detail);
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool validHeader(const NevercABITableHeader &Header, size_t Required) {
  return Header.StructSize >= Required &&
         Header.Major == NEVERC_CALLING_CONVENTION_API_MAJOR &&
         Header.Minor <= NEVERC_CALLING_CONVENTION_API_MINOR &&
         Header.Flags == 0;
}

bool validStructArray(NevercStructArrayView View, size_t ElementSize) {
  return View.Count <= 128 &&
         (View.Count == 0 ||
          (View.Data && View.ElementStride >= ElementSize &&
           View.ElementStride <= 4096));
}

bool validUInt32Array(NevercUInt32ArrayView View) {
  return View.Count <= 256 &&
         (View.Count == 0 ||
          (View.Data && View.ElementStride >= sizeof(uint32_t) &&
           View.ElementStride <= 4096));
}

bool hasRegister(
    const PluginTargetSnapshot::TargetRecord &Target,
    uint32_t RegisterNumber) {
  return RegisterNumber != 0 &&
         llvm::any_of(
             Target.Registers,
             [&](const VerifiedTargetRegister &Register) {
               return Register.RegisterNumber == RegisterNumber;
             });
}

Expected<std::vector<CallingConventionPlanLocation>>
copyLocations(
    StringRef ConventionName, StringRef Label,
    NevercStructArrayView View,
    ArrayRef<NevercABITypeDescriptor> Types,
    const PluginTargetSnapshot::TargetRecord &Target,
    bool ReturnLocations) {
  if (!validStructArray(
          View, sizeof(NevercCallingConventionLocation)))
    return planError(ConventionName, Label + " array is invalid");

  std::vector<CallingConventionPlanLocation> Result;
  Result.reserve(static_cast<size_t>(View.Count));
  const auto *Bytes = static_cast<const uint8_t *>(View.Data);
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Location =
        reinterpret_cast<const NevercCallingConventionLocation *>(
            Bytes + I * View.ElementStride);
    if (!validHeader(Location->Header, sizeof(*Location)) ||
        Location->Reserved != 0)
      return planError(ConventionName,
                       Label + " location has a bad ABI record");
    if (Location->Kind != NEVERC_CC_LOCATION_REGISTER &&
        Location->Kind != NEVERC_CC_LOCATION_STACK)
      return planError(ConventionName,
                       Label + " location has an unknown kind");
    if (Location->ValueIndex >= Types.size())
      return planError(ConventionName,
                       Label + " location has an invalid value index");
    if (Location->Size == 0 ||
        Location->Alignment == 0 ||
        !isPowerOf2_32(Location->Alignment))
      return planError(ConventionName,
                       Label + " location has an invalid size or alignment");
    constexpr NevercCallingConventionLocationFlags KnownFlags =
        NEVERC_CC_LOCATION_INDIRECT | NEVERC_CC_LOCATION_BYVAL;
    if (Location->Flags & ~KnownFlags)
      return planError(ConventionName,
                       Label + " location has unknown flags");
    if ((Location->Flags & NEVERC_CC_LOCATION_BYVAL) &&
        !(Location->Flags & NEVERC_CC_LOCATION_INDIRECT))
      return planError(
          ConventionName,
          Label + " by-value location is not indirect");

    const NevercABITypeDescriptor &Type =
        Types[Location->ValueIndex];
    const uint64_t TypeBytes =
        (static_cast<uint64_t>(Type.BitWidth) + 7) / 8;
    if (!(Location->Flags & NEVERC_CC_LOCATION_INDIRECT) &&
        (static_cast<uint64_t>(Location->PieceOffset) +
             Location->Size >
         TypeBytes))
      return planError(ConventionName,
                       Label + " piece exceeds its value");

    if (Location->Kind == NEVERC_CC_LOCATION_REGISTER) {
      if (!hasRegister(Target, Location->RegisterNumber) ||
          Location->StackOffset != 0)
        return planError(
            ConventionName,
            Label + " register location is not in the target schema");
    } else {
      if (ReturnLocations)
        return planError(
            ConventionName,
            "return values cannot use a stack result location");
      if (Location->RegisterNumber != 0 ||
          Location->StackOffset % Location->Alignment != 0)
        return planError(ConventionName,
                         Label + " stack location is misaligned");
    }

    Result.push_back(
        {Location->Kind, Location->ValueIndex,
         Location->PieceOffset, Location->Size,
         Location->Alignment, Location->RegisterNumber,
         Location->StackOffset, Location->Flags});
  }

  std::vector<SmallVector<std::pair<uint64_t, uint64_t>, 2>>
      Pieces(Types.size());
  std::set<uint32_t> Registers;
  SmallVector<std::pair<uint64_t, uint64_t>, 8> StackSlots;
  for (const CallingConventionPlanLocation &Location : Result) {
    if (Location.Kind == NEVERC_CC_LOCATION_REGISTER &&
        !Registers.insert(Location.RegisterNumber).second)
      return planError(ConventionName,
                       Label + " locations reuse a physical register");
    if (Location.Kind == NEVERC_CC_LOCATION_STACK)
      StackSlots.push_back(
          {Location.StackOffset,
           static_cast<uint64_t>(Location.StackOffset) +
               Location.Size});
    if (!(Location.Flags & NEVERC_CC_LOCATION_INDIRECT))
      Pieces[Location.ValueIndex].push_back(
          {Location.PieceOffset,
           static_cast<uint64_t>(Location.PieceOffset) +
               Location.Size});
  }
  llvm::sort(StackSlots);
  for (size_t I = 1; I < StackSlots.size(); ++I)
    if (StackSlots[I].first < StackSlots[I - 1].second)
      return planError(ConventionName,
                       Label + " stack locations overlap");

  for (size_t ValueIndex = 0; ValueIndex != Types.size();
       ++ValueIndex) {
    const NevercABITypeDescriptor &Type = Types[ValueIndex];
    const bool HasIndirect = llvm::any_of(
        Result, [&](const CallingConventionPlanLocation &Location) {
          return Location.ValueIndex == ValueIndex &&
                 (Location.Flags & NEVERC_CC_LOCATION_INDIRECT);
        });
    auto &ValuePieces = Pieces[ValueIndex];
    if (HasIndirect) {
      if (!ValuePieces.empty())
        return planError(
            ConventionName,
            Label + " value mixes indirect and direct pieces");
      continue;
    }
    if (Type.Kind == NEVERC_ABI_TYPE_VOID)
      continue;
    llvm::sort(ValuePieces);
    uint64_t Covered = 0;
    for (const auto &[Begin, End] : ValuePieces) {
      if (Begin != Covered)
        return planError(
            ConventionName,
            Label + " value pieces overlap or leave a gap");
      Covered = End;
    }
    const uint64_t TypeBytes =
        (static_cast<uint64_t>(Type.BitWidth) + 7) / 8;
    if (Covered != TypeBytes)
      return planError(ConventionName,
                       Label + " does not cover a value");
  }
  return Result;
}

Expected<std::vector<uint32_t>>
copyCalleeSaved(
    StringRef ConventionName, NevercUInt32ArrayView View,
    const PluginTargetSnapshot::TargetRecord &Target,
    ArrayRef<CallingConventionPlanLocation> Returns,
    ArrayRef<CallingConventionPlanLocation> Arguments) {
  if (!validUInt32Array(View))
    return planError(ConventionName,
                     "callee-saved register array is invalid");
  std::set<uint32_t> UsedRegisters;
  for (const CallingConventionPlanLocation &Location : Returns)
    if (Location.Kind == NEVERC_CC_LOCATION_REGISTER)
      UsedRegisters.insert(Location.RegisterNumber);
  for (const CallingConventionPlanLocation &Location : Arguments)
    if (Location.Kind == NEVERC_CC_LOCATION_REGISTER)
      UsedRegisters.insert(Location.RegisterNumber);

  std::vector<uint32_t> Result;
  Result.reserve(static_cast<size_t>(View.Count));
  std::set<uint32_t> Seen;
  const auto *Bytes = reinterpret_cast<const uint8_t *>(View.Data);
  for (uint64_t I = 0; I != View.Count; ++I) {
    const uint32_t Register =
        *reinterpret_cast<const uint32_t *>(
            Bytes + I * View.ElementStride);
    if (!hasRegister(Target, Register))
      return planError(
          ConventionName,
          "callee-saved register is not in the target schema");
    if (!Seen.insert(Register).second)
      return planError(ConventionName,
                       "callee-saved register is duplicated");
    if (UsedRegisters.count(Register))
      return planError(
          ConventionName,
          "callee-saved register overlaps an argument or return register");
    Result.push_back(Register);
  }
  return Result;
}

void serializeLocations(
    raw_ostream &OS,
    ArrayRef<CallingConventionPlanLocation> Locations) {
  for (size_t I = 0; I != Locations.size(); ++I) {
    if (I != 0)
      OS << '|';
    const CallingConventionPlanLocation &Location = Locations[I];
    OS << (Location.Kind == NEVERC_CC_LOCATION_REGISTER ? 'r' : 's')
       << ',' << Location.ValueIndex << ',' << Location.PieceOffset
       << ',' << Location.Size << ',' << Location.Alignment << ','
       << Location.RegisterNumber << ',' << Location.StackOffset << ','
       << Location.Flags;
  }
}

} // namespace

std::string MaterializedCallingConventionPlan::serialize() const {
  std::string Text;
  raw_string_ostream OS(Text);
  OS << "neverc-cc-plan-v1"
     << ";schema=" << SchemaDigest << ";target=" << TargetID.High << ':'
     << TargetID.Low << ";cc=" << CallingConventionID.High << ':'
     << CallingConventionID.Low << ";stack=" << StackAlignment
     << ";returns=";
  serializeLocations(OS, ReturnLocations);
  OS << ";arguments=";
  serializeLocations(OS, ArgumentLocations);
  OS << ";callee-saved=";
  for (size_t I = 0; I != CalleeSavedRegisters.size(); ++I) {
    if (I != 0)
      OS << ',';
    OS << CalleeSavedRegisters[I];
  }
  return OS.str();
}

Expected<MaterializedCallingConventionPlan>
CallingConventionPlanner::materialize(
    const NevercABITypeDescriptor &ReturnType,
    ArrayRef<NevercABITypeDescriptor> Parameters, bool Variadic,
    uint32_t RequiredArguments) const {
  if (!Convention.PlanCallingConvention)
    return createStringError(
        inconvertibleErrorCode(),
        "target calling convention '" +
            Convention.CanonicalName + "' has no plan callback");
  if (!sameID(Convention.TargetID, Target.ID))
    return createStringError(
        inconvertibleErrorCode(),
        "target calling convention '" +
            Convention.CanonicalName + "' belongs to another target");
  if (RequiredArguments > Parameters.size() ||
      (!Variadic && RequiredArguments != Parameters.size()))
    return createStringError(
        inconvertibleErrorCode(),
        "target calling convention planner received an invalid "
        "required argument count");
  if (Variadic)
    return planError(
        Convention.CanonicalName,
        "variadic functions are not supported");

  NevercCallingConventionQuery Query{};
  Query.Header = {sizeof(Query),
                  NEVERC_CALLING_CONVENTION_API_MAJOR,
                  NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  Query.TargetID = Target.ID;
  Query.CallingConventionID = Convention.ID;
  Query.SchemaDigest = {
      Target.Machine.SchemaDigest.data(),
      Target.Machine.SchemaDigest.size()};
  Query.Function.Header = {
      sizeof(Query.Function), NEVERC_TARGET_ABI_API_MAJOR,
      NEVERC_TARGET_ABI_API_MINOR, 0};
  Query.Function.ReturnType = ReturnType;
  Query.Function.Parameters = {
      Parameters.data(), Parameters.size(),
      sizeof(NevercABITypeDescriptor)};
  Query.Function.Variadic =
      Variadic ? NEVERC_TRUE : NEVERC_FALSE;
  Query.Function.RequiredArgumentCount = RequiredArguments;

  NevercCallingConventionPlan RawPlan{};
  RawPlan.Header = {
      sizeof(RawPlan), NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  auto Invoke = [&] {
    return Convention.PlanCallingConvention(
        Convention.CallbackUserData, &Query, &RawPlan);
  };
  Expected<NevercStatus> Status =
      Task ? Task->invokeCallback(
                 Convention.PluginID, "PlanCallingConvention", Invoke)
           : Expected<NevercStatus>(Invoke());
  if (!Status)
    return Status.takeError();
  if (!neverc_status_is_ok(*Status))
    return createStringError(
        inconvertibleErrorCode(),
        "target calling convention '" +
            Convention.CanonicalName +
            "' plan callback failed with status " +
            std::to_string(Status->Code));
  if (!validHeader(RawPlan.Header, sizeof(RawPlan)) ||
      RawPlan.Reserved32 != 0 || RawPlan.Flags != 0 ||
      RawPlan.Reserved[0] != 0 || RawPlan.Reserved[1] != 0)
    return planError(Convention.CanonicalName,
                     "plan record is invalid");

  const std::array<NevercABITypeDescriptor, 1> ReturnTypes = {
      ReturnType};
  auto Returns = copyLocations(
      Convention.CanonicalName, "return", RawPlan.ReturnLocations,
      ReturnTypes, Target, /*ReturnLocations=*/true);
  if (!Returns)
    return Returns.takeError();
  auto Arguments = copyLocations(
      Convention.CanonicalName, "argument",
      RawPlan.ArgumentLocations, Parameters, Target,
      /*ReturnLocations=*/false);
  if (!Arguments)
    return Arguments.takeError();
  auto CalleeSaved = copyCalleeSaved(
      Convention.CanonicalName, RawPlan.CalleeSavedRegisters,
      Target, *Returns, *Arguments);
  if (!CalleeSaved)
    return CalleeSaved.takeError();

  if (RawPlan.StackAlignment == 0 ||
      !isPowerOf2_32(RawPlan.StackAlignment) ||
      static_cast<uint64_t>(RawPlan.StackAlignment) * 8 >
          Target.Machine.StackAlignment ||
      Target.Machine.StackAlignment %
              (RawPlan.StackAlignment * 8) !=
          0)
    return planError(Convention.CanonicalName,
                     "stack alignment is incompatible with the target");

  MaterializedCallingConventionPlan Result;
  Result.TargetID = Target.ID;
  Result.CallingConventionID = Convention.ID;
  Result.SchemaDigest = Target.Machine.SchemaDigest;
  Result.ReturnLocations = std::move(*Returns);
  Result.ArgumentLocations = std::move(*Arguments);
  Result.CalleeSavedRegisters = std::move(*CalleeSaved);
  Result.StackAlignment = RawPlan.StackAlignment;
  return Result;
}
