#include "neverc/Plugin/Host/MCEncoderRegistry.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool validHeader(const NevercABITableHeader &Header, size_t Required) {
  return Header.StructSize >= Required &&
         Header.Major == NEVERC_MC_API_MAJOR &&
         Header.Minor <= NEVERC_MC_API_MINOR && Header.Flags == 0;
}

bool validBool(NevercBool Value) {
  return Value == NEVERC_FALSE || Value == NEVERC_TRUE;
}

Error codecError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

Error callbackError(StringRef Kind, NevercStatus Status) {
  return codecError(Kind + " callback failed with status " +
                    Twine(static_cast<uint32_t>(Status.Code)));
}

const PluginTargetSnapshot::NamedRecord *
validateTargetAndSchema(const PluginTargetSnapshot &Targets,
                        NevercTargetID TargetID,
                        NevercInterfaceID SchemaID, StringRef PluginID,
                        StringRef Kind, Error &Failure) {
  const auto *Target = Targets.findTarget(TargetID);
  const auto *Schema = Targets.findMCSchema(SchemaID);
  if (!Target) {
    Failure = codecError("plugin '" + PluginID + "' " + Kind +
                         " references an unknown Target ID");
    return nullptr;
  }
  if (!Schema || !sameID(Schema->TargetID, TargetID)) {
    Failure = codecError("plugin '" + PluginID + "' " + Kind +
                         " references an unknown MC schema ID");
    return nullptr;
  }
  if (!sameID(Target->MCSchemaID, SchemaID)) {
    Failure = codecError("plugin '" + PluginID + "' " + Kind +
                         " does not use the Target's selected MC schema");
    return nullptr;
  }
  return Schema;
}

Expected<std::vector<NevercStringView>>
makeFeatureViews(ArrayRef<StringRef> Features) {
  if (Features.size() > 4096)
    return codecError("MC codec feature set is too large");
  std::vector<NevercStringView> Result;
  Result.reserve(Features.size());
  for (StringRef Feature : Features) {
    if (Feature.empty() || Feature.contains('\0'))
      return codecError("MC codec feature set contains an invalid name");
    Result.push_back({Feature.data(), Feature.size()});
  }
  return Result;
}

struct EncodeSinkState {
  uint32_t MaximumInstructionLength = 0;
  NevercMCSchemaTokenHandle SchemaToken{};
  EncodedMCInstruction Output;
  bool MaximumExceeded = false;
  bool InvalidFixup = false;
};

NevercStatus sinkStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercStatus NEVERC_CALL writeEncodedBytes(void *Context,
                                           NevercByteView Bytes) {
  auto *State = static_cast<EncodeSinkState *>(Context);
  if (!State || (!Bytes.Data && Bytes.Length != 0) ||
      Bytes.Length > std::numeric_limits<size_t>::max())
    return sinkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Bytes.Length == 0)
    return neverc_status_ok();
  if (Bytes.Length >
      State->MaximumInstructionLength - State->Output.Bytes.size()) {
    State->MaximumExceeded = true;
    return sinkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  try {
    State->Output.Bytes.insert(State->Output.Bytes.end(), Bytes.Data,
                               Bytes.Data + Bytes.Length);
  } catch (...) {
    return sinkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL addEncodedFixup(
    void *Context, const NevercMCEncodedFixup *Fixup) {
  auto *State = static_cast<EncodeSinkState *>(Context);
  constexpr size_t Required =
      offsetof(NevercMCEncodedFixup, Addend) +
      sizeof(NevercMCEncodedFixup::Addend);
  if (!State || !Fixup || !validHeader(Fixup->Header, Required) ||
      Fixup->Width == 0 || Fixup->Width > 64 ||
      !validBool(Fixup->IsPCRelative) ||
      !validBool(Fixup->IsSigned) || !validBool(Fixup->MayRelax) ||
      Fixup->Reserved8 != 0 ||
      Fixup->Kind < NEVERC_MC_FIXUP_NONE ||
      Fixup->Kind > NEVERC_MC_FIXUP_TARGET_EXTENSION ||
      (Fixup->Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION &&
       (!sameHandle(Fixup->SchemaToken, State->SchemaToken) ||
        Fixup->TargetKind == 0)) ||
      (Fixup->Kind != NEVERC_MC_FIXUP_TARGET_EXTENSION &&
       (!neverc_handle_is_null(Fixup->SchemaToken) ||
        Fixup->TargetKind != 0))) {
    if (State)
      State->InvalidFixup = true;
    return sinkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  try {
    State->Output.Fixups.push_back(
        {Fixup->Offset,
         Fixup->Width,
         Fixup->IsPCRelative == NEVERC_TRUE,
         Fixup->IsSigned == NEVERC_TRUE,
         Fixup->MayRelax == NEVERC_TRUE,
         Fixup->Kind,
         Fixup->TargetKind,
         Fixup->OperandIndex,
         Fixup->Addend,
         nullptr});
  } catch (...) {
    return sinkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  return neverc_status_ok();
}

uint32_t genericFixupWidth(MCFixupKind Kind) {
  switch (Kind) {
  case FK_Data_1:
  case FK_PCRel_1:
  case FK_GPRel_1:
  case FK_SecRel_1:
    return 8;
  case FK_Data_2:
  case FK_PCRel_2:
  case FK_GPRel_2:
  case FK_SecRel_2:
    return 16;
  case FK_Data_4:
  case FK_PCRel_4:
  case FK_GPRel_4:
  case FK_DTPRel_4:
  case FK_TPRel_4:
  case FK_SecRel_4:
    return 32;
  case FK_Data_8:
  case FK_PCRel_8:
  case FK_GPRel_8:
  case FK_DTPRel_8:
  case FK_TPRel_8:
  case FK_SecRel_8:
    return 64;
  case FK_NONE:
  case FK_Data_leb128:
  default:
    return 0;
  }
}

bool genericFixupIsPCRelative(MCFixupKind Kind) {
  return Kind == FK_PCRel_1 || Kind == FK_PCRel_2 ||
         Kind == FK_PCRel_4 || Kind == FK_PCRel_8;
}

} // namespace

Expected<std::shared_ptr<const MCEncoderRegistry>>
MCEncoderRegistry::freeze(
    ArrayRef<MCCodecRegistrationView> Registrations,
    const PluginTargetSnapshot &Targets) {
  auto Registry = std::make_shared<MCEncoderRegistry>();
  for (const MCCodecRegistrationView &Registration : Registrations) {
    if (Registration.PluginID.empty())
      return codecError("MC codec registration has no plugin ID");

    for (const NevercMCEncoderDescriptor &Descriptor :
         Registration.Encoders) {
      constexpr size_t Required =
          offsetof(NevercMCEncoderDescriptor, DestroyUserData) +
          sizeof(NevercMCEncoderDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.ProviderID) ||
          !nonzero(Descriptor.TargetID) ||
          !nonzero(Descriptor.SchemaID) ||
          Descriptor.MaximumInstructionLength == 0 ||
          Descriptor.MaximumInstructionLength > 4096 ||
          Descriptor.Reserved != 0 || Descriptor.Flags != 0 ||
          !Descriptor.EncodeInstruction)
        return codecError("plugin '" + Registration.PluginID +
                          "' has an invalid MC encoder descriptor");
      Error Failure = Error::success();
      if (!validateTargetAndSchema(
              Targets, Descriptor.TargetID, Descriptor.SchemaID,
              Registration.PluginID, "MC encoder", Failure))
        return std::move(Failure);
      for (const EncoderRecord &Existing : Registry->Encoders) {
        if (sameID(Existing.ProviderID, Descriptor.ProviderID))
          return codecError("duplicate MC encoder provider ID");
        if (sameID(Existing.TargetID, Descriptor.TargetID) &&
            sameID(Existing.SchemaID, Descriptor.SchemaID))
          return codecError(
              "multiple MC encoders registered for one Target/schema");
      }
      Registry->Encoders.push_back(
          {Registration.PluginID.str(),
           Registration.Owner,
           Descriptor.ProviderID,
           Descriptor.TargetID,
           Descriptor.SchemaID,
           Descriptor.MaximumInstructionLength,
           Descriptor.EncodeInstruction,
           Descriptor.UserData});
    }

    for (const NevercMCDecoderDescriptor &Descriptor :
         Registration.Decoders) {
      constexpr size_t Required =
          offsetof(NevercMCDecoderDescriptor, DestroyUserData) +
          sizeof(NevercMCDecoderDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.ProviderID) ||
          !nonzero(Descriptor.TargetID) ||
          !nonzero(Descriptor.SchemaID) ||
          Descriptor.MaximumInstructionLength == 0 ||
          Descriptor.MaximumInstructionLength > 4096 ||
          Descriptor.Reserved != 0 || Descriptor.Flags != 0 ||
          !Descriptor.DecodeInstruction)
        return codecError("plugin '" + Registration.PluginID +
                          "' has an invalid MC decoder descriptor");
      Error Failure = Error::success();
      if (!validateTargetAndSchema(
              Targets, Descriptor.TargetID, Descriptor.SchemaID,
              Registration.PluginID, "MC decoder", Failure))
        return std::move(Failure);
      for (const DecoderRecord &Existing : Registry->Decoders) {
        if (sameID(Existing.ProviderID, Descriptor.ProviderID))
          return codecError("duplicate MC decoder provider ID");
        if (sameID(Existing.TargetID, Descriptor.TargetID) &&
            sameID(Existing.SchemaID, Descriptor.SchemaID))
          return codecError(
              "multiple MC decoders registered for one Target/schema");
      }
      Registry->Decoders.push_back(
          {Registration.PluginID.str(),
           Registration.Owner,
           Descriptor.ProviderID,
           Descriptor.TargetID,
           Descriptor.SchemaID,
           Descriptor.MaximumInstructionLength,
           Descriptor.DecodeInstruction,
           Descriptor.UserData});
    }
  }
  return std::shared_ptr<const MCEncoderRegistry>(std::move(Registry));
}

Expected<std::shared_ptr<const MCEncoderRegistry>>
MCEncoderRegistry::freeze(
    ArrayRef<std::shared_ptr<const PluginModule>> Modules,
    const PluginTargetSnapshot &Targets) {
  struct MaterializedRegistration {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::vector<NevercMCEncoderDescriptor> Encoders;
    std::vector<NevercMCDecoderDescriptor> Decoders;
  };
  std::vector<MaterializedRegistration> Materialized;
  Materialized.reserve(Modules.size());
  for (const std::shared_ptr<const PluginModule> &Module : Modules) {
    const PluginPublishedRegistration *Published = Module->registration();
    if (!Published)
      continue;
    MaterializedRegistration Registration;
    Registration.PluginID = Module->descriptor().PluginID;
    Registration.Owner = Module;
    for (const PluginRegistrationRecord &Record : Published->records()) {
      if (Record.Kind == PluginRegistrationKind::MCEncoder)
        Registration.Encoders.push_back(Record.MCEncoder);
      else if (Record.Kind == PluginRegistrationKind::MCDecoder)
        Registration.Decoders.push_back(Record.MCDecoder);
    }
    Materialized.push_back(std::move(Registration));
  }

  std::vector<MCCodecRegistrationView> Views;
  Views.reserve(Materialized.size());
  for (const MaterializedRegistration &Registration : Materialized)
    Views.push_back({Registration.PluginID, Registration.Owner,
                     Registration.Encoders, Registration.Decoders});
  return freeze(Views, Targets);
}

const MCEncoderRegistry::EncoderRecord *
MCEncoderRegistry::findEncoder(NevercTargetID Target,
                               NevercInterfaceID Schema) const {
  for (const EncoderRecord &Record : Encoders)
    if (sameID(Record.TargetID, Target) &&
        sameID(Record.SchemaID, Schema))
      return &Record;
  return nullptr;
}

const MCEncoderRegistry::DecoderRecord *
MCEncoderRegistry::findDecoder(NevercTargetID Target,
                               NevercInterfaceID Schema) const {
  for (const DecoderRecord &Record : Decoders)
    if (sameID(Record.TargetID, Target) &&
        sameID(Record.SchemaID, Schema))
      return &Record;
  return nullptr;
}

struct PluginMCCodeEmitter::Impl {
  std::shared_ptr<const MCEncoderRegistry> Registry;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  MCEncoderRegistry::EncoderRecord Encoder;
  const PluginTargetSnapshot::NamedRecord *Schema = nullptr;
};

PluginMCCodeEmitter::PluginMCCodeEmitter(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginMCCodeEmitter::~PluginMCCodeEmitter() = default;

Expected<std::unique_ptr<PluginMCCodeEmitter>>
PluginMCCodeEmitter::create(
    std::shared_ptr<const MCEncoderRegistry> Registry,
    std::shared_ptr<const PluginTargetSnapshot> Targets,
    NevercTargetID TargetID) {
  if (!Registry || !Targets)
    return codecError("MC encoder creation requires frozen registries");
  const auto *Target = Targets->findTarget(TargetID);
  if (!Target)
    return codecError("MC encoder Target is not registered");
  const auto *Schema = Targets->findMCSchema(Target->MCSchemaID);
  if (!Schema)
    return codecError("MC encoder Target has no registered schema");
  const auto *Encoder =
      Registry->findEncoder(TargetID, Target->MCSchemaID);
  if (!Encoder)
    return codecError("MC encoder provider is not registered");
  auto State = std::make_unique<Impl>();
  State->Registry = std::move(Registry);
  State->Targets = std::move(Targets);
  State->Encoder = *Encoder;
  State->Schema = Schema;
  return std::unique_ptr<PluginMCCodeEmitter>(
      new PluginMCCodeEmitter(std::move(State)));
}

Expected<EncodedMCInstruction>
PluginMCCodeEmitter::encode(
    PluginTaskContext &Task, const MCInst &Instruction, uint64_t Address,
    ArrayRef<StringRef> Features) const {
  auto FeatureViews = makeFeatureViews(Features);
  if (!FeatureViews)
    return FeatureViews.takeError();

  PluginMCUnit Unit;
  MCInst &Local =
      Unit.append(std::make_unique<MCInst>(Instruction));
  MCPluginBridge Bridge(Task, Unit, State->Schema);
  auto UnitHandle = Bridge.unit();
  if (!UnitHandle)
    return UnitHandle.takeError();
  auto SchemaToken = Bridge.schemaToken();
  if (!SchemaToken)
    return SchemaToken.takeError();
  auto InstructionHandle = Bridge.wrapInstruction(Local);
  if (!InstructionHandle)
    return InstructionHandle.takeError();

  NevercMCEncodeRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_MC_API_MAJOR,
                    NEVERC_MC_API_MINOR, 0};
  Request.MC = &Bridge.api();
  Request.Task = Task.handle();
  Request.Unit = *UnitHandle;
  Request.Instruction = *InstructionHandle;
  Request.SchemaToken = *SchemaToken;
  Request.Address = Address;
  Request.Features = {FeatureViews->data(),
                      static_cast<uint64_t>(FeatureViews->size()),
                      sizeof(NevercStringView)};

  EncodeSinkState SinkState;
  SinkState.MaximumInstructionLength =
      State->Encoder.MaximumInstructionLength;
  SinkState.SchemaToken = *SchemaToken;
  NevercMCEncodeSink Sink{};
  Sink.Header = {sizeof(Sink), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  Sink.Context = &SinkState;
  Sink.WriteBytes = writeEncodedBytes;
  Sink.AddFixup = addEncodedFixup;

  NevercStatus Status;
  try {
    Status = State->Encoder.EncodeInstruction(
        State->Encoder.UserData, &Request, &Sink);
  } catch (...) {
    return codecError("MC encoder callback raised an exception");
  }
  if (SinkState.MaximumExceeded)
    return codecError("MC encoder exceeded maximum instruction length");
  if (SinkState.InvalidFixup)
    return codecError("MC encoder produced an invalid fixup");
  if (Status.Code != NEVERC_STATUS_OK)
    return callbackError("MC encoder", Status);
  if (SinkState.Output.Bytes.empty())
    return codecError("MC encoder produced an empty instruction");

  for (const EncodedMCFixup &Fixup : SinkState.Output.Fixups) {
    const uint64_t WidthBytes = (Fixup.Width + 7) / 8;
    if (Fixup.Offset > SinkState.Output.Bytes.size() ||
        WidthBytes > SinkState.Output.Bytes.size() - Fixup.Offset)
      return codecError("MC encoder fixup is outside encoded bytes");
    if (Fixup.OperandIndex != NEVERC_MC_NO_FIXUP_OPERAND &&
        Fixup.OperandIndex >= Instruction.getNumOperands())
      return codecError("MC encoder fixup references an invalid operand");
    if (Fixup.Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION) {
      if (Fixup.TargetKind == 0)
        return codecError("target MC encoder fixup has no target kind");
    } else if (Fixup.TargetKind != 0) {
      return codecError("generic MC encoder fixup has a target kind");
    }
  }
  return std::move(SinkState.Output);
}

struct PluginMCDecoder::Impl {
  std::shared_ptr<const MCEncoderRegistry> Registry;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  MCEncoderRegistry::DecoderRecord Decoder;
  const PluginTargetSnapshot::NamedRecord *Schema = nullptr;
};

PluginMCDecoder::PluginMCDecoder(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginMCDecoder::~PluginMCDecoder() = default;

Expected<std::unique_ptr<PluginMCDecoder>>
PluginMCDecoder::create(
    std::shared_ptr<const MCEncoderRegistry> Registry,
    std::shared_ptr<const PluginTargetSnapshot> Targets,
    NevercTargetID TargetID) {
  if (!Registry || !Targets)
    return codecError("MC decoder creation requires frozen registries");
  const auto *Target = Targets->findTarget(TargetID);
  if (!Target)
    return codecError("MC decoder Target is not registered");
  const auto *Schema = Targets->findMCSchema(Target->MCSchemaID);
  if (!Schema)
    return codecError("MC decoder Target has no registered schema");
  const auto *Decoder =
      Registry->findDecoder(TargetID, Target->MCSchemaID);
  if (!Decoder)
    return codecError("MC decoder provider is not registered");
  auto State = std::make_unique<Impl>();
  State->Registry = std::move(Registry);
  State->Targets = std::move(Targets);
  State->Decoder = *Decoder;
  State->Schema = Schema;
  return std::unique_ptr<PluginMCDecoder>(
      new PluginMCDecoder(std::move(State)));
}

Expected<DecodedMCInstruction>
PluginMCDecoder::decode(
    PluginTaskContext &Task, ArrayRef<uint8_t> Bytes, uint64_t Address,
    ArrayRef<StringRef> Features) const {
  if (Bytes.empty())
    return codecError("MC decoder input is empty");
  auto FeatureViews = makeFeatureViews(Features);
  if (!FeatureViews)
    return FeatureViews.takeError();
  Bytes = Bytes.take_front(std::min<size_t>(
      Bytes.size(), State->Decoder.MaximumInstructionLength));

  PluginMCUnit Unit;
  MCPluginBridge Bridge(Task, Unit, State->Schema);
  auto UnitHandle = Bridge.unit();
  if (!UnitHandle)
    return UnitHandle.takeError();
  auto SchemaToken = Bridge.schemaToken();
  if (!SchemaToken)
    return SchemaToken.takeError();
  auto Mutation = Bridge.beginMutation();
  if (!Mutation)
    return Mutation.takeError();
  auto Abandon = make_scope_exit(
      [&] { (void)Bridge.abandonMutation(*Mutation); });

  NevercMCDecodeRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_MC_API_MAJOR,
                    NEVERC_MC_API_MINOR, 0};
  Request.MC = &Bridge.api();
  Request.Task = Task.handle();
  Request.Unit = *UnitHandle;
  Request.Mutation = *Mutation;
  Request.SchemaToken = *SchemaToken;
  Request.Bytes = {Bytes.data(), Bytes.size()};
  Request.Address = Address;
  Request.Features = {FeatureViews->data(),
                      static_cast<uint64_t>(FeatureViews->size()),
                      sizeof(NevercStringView)};
  NevercMCDecodeResult Result{};
  Result.Header = {sizeof(Result), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};

  NevercStatus Status;
  try {
    Status = State->Decoder.DecodeInstruction(
        State->Decoder.UserData, &Request, &Result);
  } catch (...) {
    return codecError("MC decoder callback raised an exception");
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return callbackError("MC decoder", Status);
  if (!validHeader(Result.Header, sizeof(Result)) ||
      Result.Reserved != 0 ||
      (Result.Outcome != NEVERC_MC_DECODE_SUCCESS &&
       Result.Outcome != NEVERC_MC_DECODE_SOFT_FAIL &&
       Result.Outcome != NEVERC_MC_DECODE_UNKNOWN &&
       Result.Outcome != NEVERC_MC_DECODE_FAIL) ||
      Result.ConsumedBytes > Bytes.size() ||
      (Result.Outcome != NEVERC_MC_DECODE_FAIL &&
       Result.ConsumedBytes == 0))
    return codecError("MC decoder produced an invalid result");

  DecodedMCInstruction Decoded;
  Decoded.Outcome = Result.Outcome;
  Decoded.ConsumedBytes = Result.ConsumedBytes;
  if (Result.Outcome == NEVERC_MC_DECODE_UNKNOWN ||
      Result.Outcome == NEVERC_MC_DECODE_FAIL) {
    if (!neverc_handle_is_null(Result.Instruction))
      return codecError(
          "unsuccessful MC decoder result returned an instruction");
    return Decoded;
  }
  if (neverc_handle_is_null(Result.Instruction))
    return codecError(
        "recognized MC decoder result has no instruction");
  MCInst *Instruction = nullptr;
  Status = Bridge.resolveInstruction(Result.Instruction, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK || !Instruction)
    return codecError(
        "MC decoder returned a foreign or stale instruction");
  Decoded.Instruction = std::make_unique<MCInst>(*Instruction);
  return Decoded;
}

Expected<EncodedMCInstruction>
BuiltinMCCodeEmitterAdapter::encode(
    const MCCodeEmitter &Emitter, const MCInst &Instruction,
    const MCSubtargetInfo &Subtarget) {
  SmallVector<char, 16> Bytes;
  SmallVector<MCFixup, 4> Fixups;
  try {
    Emitter.encodeInstruction(Instruction, Bytes, Fixups, Subtarget);
  } catch (...) {
    return codecError("built-in MC encoder raised an exception");
  }

  EncodedMCInstruction Result;
  Result.Bytes.reserve(Bytes.size());
  for (char Byte : Bytes)
    Result.Bytes.push_back(static_cast<uint8_t>(
        static_cast<unsigned char>(Byte)));
  Result.Fixups.reserve(Fixups.size());
  for (const MCFixup &Fixup : Fixups) {
    const MCFixupKind NativeKind = Fixup.getKind();
    const bool TargetKind = NativeKind >= FirstTargetFixupKind;
    Result.Fixups.push_back(
        {Fixup.getOffset(),
         genericFixupWidth(NativeKind),
         genericFixupIsPCRelative(NativeKind),
         false,
         false,
         TargetKind
             ? NEVERC_MC_FIXUP_TARGET_EXTENSION
             : static_cast<NevercMCFixupKind>(
                   NEVERC_MC_FIXUP_NONE +
                   static_cast<uint32_t>(NativeKind)),
         TargetKind ? Fixup.getTargetKind() : 0,
         NEVERC_MC_NO_FIXUP_OPERAND,
         0,
         Fixup.getValue()});
  }
  return Result;
}

} // namespace neverc::plugin
