#include "neverc/Plugin/Host/MCEncoderRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TargetID{UINT64_C(0x4e43504d43454e43), 1};
constexpr NevercInterfaceID SchemaID{UINT64_C(0x4e43504d43534348), 14};
constexpr NevercInterfaceID EncoderID{UINT64_C(0x4e43504d43454e43), 14};
constexpr NevercInterfaceID DecoderID{UINT64_C(0x4e43504d43444543), 14};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

class MCTaskScope {
public:
  MCTaskScope()
      : Services("neverc-plugin-mc-codec-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~MCTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

NevercTargetDescriptor targetDescriptor() {
  NevercTargetDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Descriptor.TargetID = TargetID;
  Descriptor.CanonicalName = view("test.mc-codec-target");
  Descriptor.MCSchemaID = SchemaID;
  Descriptor.Machine.Header = {
      sizeof(Descriptor.Machine), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Descriptor.Machine.RawTriple = view("test-unknown-none-none");
  Descriptor.Machine.Architecture = view("test");
  Descriptor.Machine.DataLayout =
      view("e-p:64:64-i64:64-n32:64-S128");
  Descriptor.Machine.DefaultCPU = view("generic");
  Descriptor.Machine.SchemaDigest = view(
      "1414141414141414141414141414141414141414141414141414141414141414");
  Descriptor.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Descriptor.Machine.SupportedCodeModels =
      NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Descriptor.Machine.DefaultRelocationModel =
      NEVERC_TARGET_RELOCATION_STATIC;
  Descriptor.Machine.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_SMALL;
  Descriptor.Machine.ExceptionModel = NEVERC_TARGET_EXCEPTION_NONE;
  Descriptor.Machine.UnwindModel = NEVERC_TARGET_UNWIND_NONE;
  Descriptor.Machine.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Descriptor.Machine.PointerWidth = 64;
  Descriptor.Machine.IntWidth = 32;
  Descriptor.Machine.LongWidth = 64;
  Descriptor.Machine.LongLongWidth = 64;
  Descriptor.Machine.StackAlignment = 128;
  Descriptor.Machine.MaximumAtomicWidth = 64;
  Descriptor.Machine.MaximumVectorAlignment = 128;
  Descriptor.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Descriptor.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Descriptor.Machine.DefaultExecutionLevel =
      NEVERC_TARGET_EXECUTION_USER;
  Descriptor.Machine.TLSSupported = NEVERC_TRUE;
  return Descriptor;
}

NevercMCSchemaDescriptor schemaDescriptor() {
  static const NevercMCSchemaValueDescriptor Opcodes[] = {{
      {sizeof(NevercMCSchemaValueDescriptor), NEVERC_MC_API_MAJOR,
       NEVERC_MC_API_MINOR, 0},
      10,
      100,
      {"test.codec", 10},
      0,
  }};
  NevercMCSchemaDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_MC_API_MAJOR,
                       NEVERC_MC_API_MINOR, 0};
  Descriptor.SchemaID = SchemaID;
  Descriptor.TargetID = TargetID;
  Descriptor.CanonicalName = view("test.mc-codec-schema");
  Descriptor.Digest = view(
      "1414141414141414141414141414141414141414141414141414141414141414");
  Descriptor.Opcodes = {Opcodes, 1, sizeof(Opcodes[0])};
  return Descriptor;
}

NevercStatus NEVERC_CALL encodeInstruction(
    void *, const NevercMCEncodeRequest *Request,
    const NevercMCEncodeSink *Sink) {
  if (!Request || !Sink || !Request->MC ||
      Request->Features.Count != 1)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  NevercMCInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  NevercStatus Result = Request->MC->GetInstructionInfo(
      Request->MC->Context, Request->Task, Request->Instruction, &Info);
  if (Result.Code != NEVERC_STATUS_OK || Info.Opcode != 10 ||
      Info.OperandCount != 1)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  NevercMCOperandHandle Operand{};
  Result = Request->MC->GetInstructionOperand(
      Request->MC->Context, Request->Task, Request->Instruction, 0,
      &Operand);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  NevercMCOperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Result = Request->MC->GetOperandValue(
      Request->MC->Context, Request->Task, Operand, &Value);
  if (Result.Code != NEVERC_STATUS_OK ||
      Value.Kind != NEVERC_MC_OPERAND_IMMEDIATE)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  const std::array<uint8_t, 2> Bytes = {
      UINT8_C(0xa0), static_cast<uint8_t>(Value.Payload.Immediate)};
  Result = Sink->WriteBytes(Sink->Context, {Bytes.data(), Bytes.size()});
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;

  NevercMCEncodedFixup Fixup{};
  Fixup.Header = {sizeof(Fixup), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Fixup.Offset = 1;
  Fixup.Width = 8;
  Fixup.Kind = NEVERC_MC_FIXUP_DATA_1;
  Fixup.OperandIndex = 0;
  Fixup.Addend = 7;
  return Sink->AddFixup(Sink->Context, &Fixup);
}

NevercStatus NEVERC_CALL malformedFixupEncoder(
    void *, const NevercMCEncodeRequest *,
    const NevercMCEncodeSink *Sink) {
  const uint8_t Byte = UINT8_C(0xa0);
  NevercStatus Result =
      Sink->WriteBytes(Sink->Context, {&Byte, 1});
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  NevercMCEncodedFixup Fixup{};
  Fixup.Header = {sizeof(Fixup), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Fixup.Offset = 1;
  Fixup.Width = 8;
  Fixup.Kind = NEVERC_MC_FIXUP_DATA_1;
  Fixup.OperandIndex = NEVERC_MC_NO_FIXUP_OPERAND;
  return Sink->AddFixup(Sink->Context, &Fixup);
}

NevercStatus NEVERC_CALL foreignSchemaFixupEncoder(
    void *, const NevercMCEncodeRequest *,
    const NevercMCEncodeSink *Sink) {
  const uint8_t Byte = UINT8_C(0xa0);
  NevercStatus Result =
      Sink->WriteBytes(Sink->Context, {&Byte, 1});
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  NevercMCEncodedFixup Fixup{};
  Fixup.Header = {sizeof(Fixup), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Fixup.Offset = 0;
  Fixup.Width = 8;
  Fixup.Kind = NEVERC_MC_FIXUP_TARGET_EXTENSION;
  Fixup.SchemaToken = {UINT64_C(0xdead), UINT64_C(0xbeef)};
  Fixup.TargetKind = 30;
  Fixup.OperandIndex = NEVERC_MC_NO_FIXUP_OPERAND;
  return Sink->AddFixup(Sink->Context, &Fixup);
}

NevercStatus NEVERC_CALL failingEncoder(
    void *, const NevercMCEncodeRequest *,
    const NevercMCEncodeSink *) {
  return status(NEVERC_STATUS_VERIFICATION_FAILED);
}

NevercStatus NEVERC_CALL decodeInstruction(
    void *, const NevercMCDecodeRequest *Request,
    NevercMCDecodeResult *OutResult) {
  if (!Request || !OutResult || !Request->MC ||
      Request->Bytes.Length == 0)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Request->Bytes.Data[0] == UINT8_C(0xfe)) {
    OutResult->Outcome = NEVERC_MC_DECODE_FAIL;
    return neverc_status_ok();
  }
  const bool SoftFail = Request->Bytes.Data[0] == UINT8_C(0xa1);
  if (Request->Bytes.Data[0] != UINT8_C(0xa0) && !SoftFail) {
    OutResult->Outcome = NEVERC_MC_DECODE_UNKNOWN;
    OutResult->ConsumedBytes = 1;
    return neverc_status_ok();
  }
  if (Request->Bytes.Length < 2)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  NevercMCInstHandle Instruction{};
  NevercStatus Result = Request->MC->CreateInstruction(
      Request->MC->Context, Request->Task, Request->Mutation,
      Request->SchemaToken, 10, &Instruction);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;

  NevercMCOperandValue Operand{};
  Operand.Header = {sizeof(Operand), NEVERC_MC_API_MAJOR,
                    NEVERC_MC_API_MINOR, 0};
  Operand.Kind = NEVERC_MC_OPERAND_IMMEDIATE;
  Operand.Payload.Immediate = Request->Bytes.Data[1];
  Result = Request->MC->AppendOperand(
      Request->MC->Context, Request->Task, Request->Mutation,
      Instruction, &Operand);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;

  OutResult->Outcome =
      SoftFail ? NEVERC_MC_DECODE_SOFT_FAIL : NEVERC_MC_DECODE_SUCCESS;
  OutResult->ConsumedBytes = 2;
  OutResult->Instruction = Instruction;
  return neverc_status_ok();
}

struct CodecEnvironment {
  MCTaskScope Scope;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  std::shared_ptr<const MCEncoderRegistry> Codecs;

  bool initialize(
      uint32_t MaxInstructionLength = 8,
      NevercMCEncodeInstructionFn EncoderCallback = encodeInstruction,
      NevercMCDecodeInstructionFn DecoderCallback = decodeInstruction,
      bool IncludeDecoder = true) {
    if (!Scope.initialize())
      return false;

    NevercTargetDescriptor Target = targetDescriptor();
    NevercMCSchemaDescriptor Schema = schemaDescriptor();
    PluginTargetRegistrationView TargetRegistration;
    TargetRegistration.PluginID = "org.neverc.test.mc-codec";
    TargetRegistration.Targets = ArrayRef<NevercTargetDescriptor>(Target);
    TargetRegistration.MCSchemas =
        ArrayRef<NevercMCSchemaDescriptor>(Schema);
    auto FrozenTargets = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(TargetRegistration),
        PluginTargetRequest{});
    if (!FrozenTargets) {
      ADD_FAILURE() << errorText(FrozenTargets.takeError());
      return false;
    }
    Targets = std::move(*FrozenTargets);

    NevercMCEncoderDescriptor Encoder{};
    Encoder.Header = {sizeof(Encoder), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
    Encoder.ProviderID = EncoderID;
    Encoder.TargetID = TargetID;
    Encoder.SchemaID = SchemaID;
    Encoder.MaximumInstructionLength = MaxInstructionLength;
    Encoder.EncodeInstruction = EncoderCallback;
    NevercMCDecoderDescriptor Decoder{};
    Decoder.Header = {sizeof(Decoder), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
    Decoder.ProviderID = DecoderID;
    Decoder.TargetID = TargetID;
    Decoder.SchemaID = SchemaID;
    Decoder.MaximumInstructionLength = MaxInstructionLength;
    Decoder.DecodeInstruction = DecoderCallback;
    MCCodecRegistrationView CodecRegistration;
    CodecRegistration.PluginID = "org.neverc.test.mc-codec";
    CodecRegistration.Encoders =
        ArrayRef<NevercMCEncoderDescriptor>(Encoder);
    if (IncludeDecoder)
      CodecRegistration.Decoders =
          ArrayRef<NevercMCDecoderDescriptor>(Decoder);
    auto FrozenCodecs = MCEncoderRegistry::freeze(
        ArrayRef<MCCodecRegistrationView>(CodecRegistration), *Targets);
    if (!FrozenCodecs) {
      ADD_FAILURE() << errorText(FrozenCodecs.takeError());
      return false;
    }
    Codecs = std::move(*FrozenCodecs);
    return true;
  }
};

TEST(PluginMCEncodeDecodeTest, PluginEncoderEmitsBytesAndFixups) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Emitter = PluginMCCodeEmitter::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Emitter))
      << errorText(Emitter.takeError());

  MCInst Instruction;
  Instruction.setOpcode(100);
  Instruction.addOperand(MCOperand::createImm(0x2a));
  const std::array<StringRef, 1> Features = {"codec"};
  auto Encoded = (*Emitter)->encode(
      Environment.Scope.task(), Instruction, UINT64_C(0x1000), Features);
  ASSERT_TRUE(static_cast<bool>(Encoded))
      << errorText(Encoded.takeError());
  ASSERT_EQ(Encoded->Bytes.size(), 2U);
  EXPECT_EQ(Encoded->Bytes[0], UINT8_C(0xa0));
  EXPECT_EQ(Encoded->Bytes[1], UINT8_C(0x2a));
  ASSERT_EQ(Encoded->Fixups.size(), 1U);
  EXPECT_EQ(Encoded->Fixups[0].Offset, 1U);
  EXPECT_EQ(Encoded->Fixups[0].Width, 8U);
  EXPECT_EQ(Encoded->Fixups[0].Kind, NEVERC_MC_FIXUP_DATA_1);
  EXPECT_EQ(Encoded->Fixups[0].OperandIndex, 0U);
  EXPECT_EQ(Encoded->Fixups[0].Addend, 7);
}

TEST(PluginMCEncodeDecodeTest, EnforcesMaximumInstructionLength) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize(1));
  auto Emitter = PluginMCCodeEmitter::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Emitter))
      << errorText(Emitter.takeError());

  MCInst Instruction;
  Instruction.setOpcode(100);
  Instruction.addOperand(MCOperand::createImm(1));
  const std::array<StringRef, 1> Features = {"codec"};
  auto Encoded = (*Emitter)->encode(
      Environment.Scope.task(), Instruction, 0, Features);
  ASSERT_FALSE(static_cast<bool>(Encoded));
  EXPECT_NE(errorText(Encoded.takeError()).find(
                "maximum instruction length"),
            std::string::npos);
}

TEST(PluginMCEncodeDecodeTest, RejectsMalformedAndForeignSchemaFixups) {
  MCInst Instruction;
  Instruction.setOpcode(100);

  CodecEnvironment Malformed;
  ASSERT_TRUE(Malformed.initialize(8, malformedFixupEncoder));
  auto MalformedEmitter = PluginMCCodeEmitter::create(
      Malformed.Codecs, Malformed.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(MalformedEmitter))
      << errorText(MalformedEmitter.takeError());
  auto MalformedResult = (*MalformedEmitter)->encode(
      Malformed.Scope.task(), Instruction, 0, {});
  ASSERT_FALSE(static_cast<bool>(MalformedResult));
  EXPECT_NE(errorText(MalformedResult.takeError()).find(
                "outside encoded bytes"),
            std::string::npos);

  CodecEnvironment Foreign;
  ASSERT_TRUE(Foreign.initialize(8, foreignSchemaFixupEncoder));
  auto ForeignEmitter = PluginMCCodeEmitter::create(
      Foreign.Codecs, Foreign.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(ForeignEmitter))
      << errorText(ForeignEmitter.takeError());
  auto ForeignResult = (*ForeignEmitter)->encode(
      Foreign.Scope.task(), Instruction, 0, {});
  ASSERT_FALSE(static_cast<bool>(ForeignResult));
  EXPECT_NE(errorText(ForeignResult.takeError()).find("invalid fixup"),
            std::string::npos);
}

TEST(PluginMCEncodeDecodeTest, SurfacesCallbackErrorAndIllegalOpcode) {
  CodecEnvironment Failing;
  ASSERT_TRUE(Failing.initialize(8, failingEncoder));
  auto FailingEmitter = PluginMCCodeEmitter::create(
      Failing.Codecs, Failing.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(FailingEmitter))
      << errorText(FailingEmitter.takeError());
  MCInst Instruction;
  Instruction.setOpcode(100);
  auto Failed = (*FailingEmitter)->encode(
      Failing.Scope.task(), Instruction, 0, {});
  ASSERT_FALSE(static_cast<bool>(Failed));
  EXPECT_NE(errorText(Failed.takeError()).find("callback failed"),
            std::string::npos);

  CodecEnvironment Illegal;
  ASSERT_TRUE(Illegal.initialize());
  auto IllegalEmitter = PluginMCCodeEmitter::create(
      Illegal.Codecs, Illegal.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(IllegalEmitter))
      << errorText(IllegalEmitter.takeError());
  Instruction.setOpcode(999);
  Instruction.addOperand(MCOperand::createImm(1));
  const std::array<StringRef, 1> Features = {"codec"};
  auto Rejected = (*IllegalEmitter)->encode(
      Illegal.Scope.task(), Instruction, 0, Features);
  ASSERT_FALSE(static_cast<bool>(Rejected));
  EXPECT_NE(errorText(Rejected.takeError()).find("callback failed"),
            std::string::npos);
}

TEST(PluginMCEncodeDecodeTest, DecoderDistinguishesRecognizedAndUnknownBytes) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Decoder = PluginMCDecoder::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Decoder))
      << errorText(Decoder.takeError());

  const std::array<uint8_t, 2> Known = {UINT8_C(0xa0), UINT8_C(0x2a)};
  auto Decoded = (*Decoder)->decode(
      Environment.Scope.task(), Known, UINT64_C(0x2000), {});
  ASSERT_TRUE(static_cast<bool>(Decoded))
      << errorText(Decoded.takeError());
  EXPECT_EQ(Decoded->Outcome, NEVERC_MC_DECODE_SUCCESS);
  EXPECT_EQ(Decoded->ConsumedBytes, 2U);
  ASSERT_NE(Decoded->Instruction, nullptr);
  EXPECT_EQ(Decoded->Instruction->getOpcode(), 100U);
  ASSERT_EQ(Decoded->Instruction->getNumOperands(), 1U);
  EXPECT_EQ(Decoded->Instruction->getOperand(0).getImm(), 0x2a);

  auto Emitter = PluginMCCodeEmitter::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Emitter))
      << errorText(Emitter.takeError());
  const std::array<StringRef, 1> Features = {"codec"};
  auto Reencoded = (*Emitter)->encode(
      Environment.Scope.task(), *Decoded->Instruction,
      UINT64_C(0x2000), Features);
  ASSERT_TRUE(static_cast<bool>(Reencoded))
      << errorText(Reencoded.takeError());
  EXPECT_EQ(Reencoded->Bytes,
            std::vector<uint8_t>(Known.begin(), Known.end()));

  const std::array<uint8_t, 1> Unknown = {UINT8_C(0xff)};
  auto UnknownResult = (*Decoder)->decode(
      Environment.Scope.task(), Unknown, UINT64_C(0x2002), {});
  ASSERT_TRUE(static_cast<bool>(UnknownResult))
      << errorText(UnknownResult.takeError());
  EXPECT_EQ(UnknownResult->Outcome, NEVERC_MC_DECODE_UNKNOWN);
  EXPECT_EQ(UnknownResult->ConsumedBytes, 1U);
  EXPECT_EQ(UnknownResult->Instruction, nullptr);

  const std::array<uint8_t, 2> Soft = {UINT8_C(0xa1), UINT8_C(0x11)};
  auto SoftResult = (*Decoder)->decode(
      Environment.Scope.task(), Soft, UINT64_C(0x2003), {});
  ASSERT_TRUE(static_cast<bool>(SoftResult))
      << errorText(SoftResult.takeError());
  EXPECT_EQ(SoftResult->Outcome, NEVERC_MC_DECODE_SOFT_FAIL);
  EXPECT_EQ(SoftResult->ConsumedBytes, 2U);
  ASSERT_NE(SoftResult->Instruction, nullptr);
  EXPECT_EQ(SoftResult->Instruction->getOperand(0).getImm(), 0x11);

  const std::array<uint8_t, 1> Failed = {UINT8_C(0xfe)};
  auto FailedResult = (*Decoder)->decode(
      Environment.Scope.task(), Failed, UINT64_C(0x2005), {});
  ASSERT_TRUE(static_cast<bool>(FailedResult))
      << errorText(FailedResult.takeError());
  EXPECT_EQ(FailedResult->Outcome, NEVERC_MC_DECODE_FAIL);
  EXPECT_EQ(FailedResult->ConsumedBytes, 0U);
  EXPECT_EQ(FailedResult->Instruction, nullptr);
}

TEST(PluginMCEncodeDecodeTest, DecoderRejectsShortBuffer) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Decoder = PluginMCDecoder::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Decoder))
      << errorText(Decoder.takeError());
  const std::array<uint8_t, 1> Short = {UINT8_C(0xa0)};
  auto Decoded =
      (*Decoder)->decode(Environment.Scope.task(), Short, 0, {});
  ASSERT_FALSE(static_cast<bool>(Decoded));
  EXPECT_NE(errorText(Decoded.takeError()).find("callback failed"),
            std::string::npos);
}

TEST(PluginMCEncodeDecodeTest, DecodeCapabilityNegotiatesIndependently) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize(
      8, encodeInstruction, decodeInstruction, false));
  auto Emitter = PluginMCCodeEmitter::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Emitter))
      << errorText(Emitter.takeError());
  auto Decoder = PluginMCDecoder::create(
      Environment.Codecs, Environment.Targets, TargetID);
  ASSERT_FALSE(static_cast<bool>(Decoder));
  EXPECT_NE(errorText(Decoder.takeError()).find("not registered"),
            std::string::npos);
}

TEST(PluginMCEncodeDecodeTest, RegistryRejectsForeignSchema) {
  CodecEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  NevercMCEncoderDescriptor Encoder{};
  Encoder.Header = {sizeof(Encoder), NEVERC_MC_API_MAJOR,
                    NEVERC_MC_API_MINOR, 0};
  Encoder.ProviderID = {EncoderID.High, EncoderID.Low + 1};
  Encoder.TargetID = TargetID;
  Encoder.SchemaID = {UINT64_C(0xdead), UINT64_C(0xbeef)};
  Encoder.MaximumInstructionLength = 8;
  Encoder.EncodeInstruction = encodeInstruction;
  MCCodecRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.mc-foreign-schema";
  Registration.Encoders =
      ArrayRef<NevercMCEncoderDescriptor>(Encoder);
  auto Frozen = MCEncoderRegistry::freeze(
      ArrayRef<MCCodecRegistrationView>(Registration),
      *Environment.Targets);
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("unknown MC schema"),
            std::string::npos);
}

class TestBuiltinEmitter final : public MCCodeEmitter {
public:
  void encodeInstruction(const MCInst &, SmallVectorImpl<char> &Bytes,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &) const override {
    Bytes.push_back(static_cast<char>(0xde));
    Bytes.push_back(static_cast<char>(0xad));
    Fixups.push_back(MCFixup::create(1, nullptr, FK_Data_1));
  }
};

TEST(PluginMCEncodeDecodeTest, BuiltinAdapterPreservesLLVMEncoding) {
  TestBuiltinEmitter Emitter;
  MCInst Instruction;
  MCSubtargetInfo STI(Triple("x86_64-unknown-none"), "generic", "generic",
                      "", {}, {}, nullptr, nullptr, nullptr, nullptr,
                      nullptr, nullptr);
  auto Encoded =
      BuiltinMCCodeEmitterAdapter::encode(Emitter, Instruction, STI);
  ASSERT_TRUE(static_cast<bool>(Encoded))
      << errorText(Encoded.takeError());
  ASSERT_EQ(Encoded->Bytes.size(), 2U);
  EXPECT_EQ(Encoded->Bytes[0], UINT8_C(0xde));
  EXPECT_EQ(Encoded->Bytes[1], UINT8_C(0xad));
  ASSERT_EQ(Encoded->Fixups.size(), 1U);
  EXPECT_EQ(Encoded->Fixups[0].Offset, 1U);
  EXPECT_EQ(Encoded->Fixups[0].Width, 8U);
  EXPECT_EQ(Encoded->Fixups[0].Kind, NEVERC_MC_FIXUP_DATA_1);
}

} // namespace
