#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) {
  auto Message = toString(std::move(E));
  return Message.str().str();
}

class IRDataScope {
public:
  IRDataScope()
      : Services("neverc-plugin-ir-data-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorMessage(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorMessage(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorMessage(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!CreatedTask) {
      ADD_FAILURE() << errorMessage(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    auto CreatedBridge = IRPluginBridge::create(*Task, "ir-data");
    if (!CreatedBridge) {
      ADD_FAILURE() << errorMessage(CreatedBridge.takeError());
      return false;
    }
    Bridge = std::move(*CreatedBridge);
    return true;
  }

  ~IRDataScope() {
    Bridge.reset();
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  IRPluginBridge &bridge() { return *Bridge; }
  NevercTaskHandle taskHandle() const { return Task->handle(); }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<IRPluginBridge> Bridge;
};

TEST(PluginIRDataTest, CreatesEveryPublicTypeFamily) {
  IRDataScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  auto Void = Bridge.getPrimitiveType(NEVERC_IR_TYPE_VOID);
  auto Float = Bridge.getPrimitiveType(NEVERC_IR_TYPE_FLOAT);
  auto Token = Bridge.getPrimitiveType(NEVERC_IR_TYPE_TOKEN);
  auto Metadata = Bridge.getPrimitiveType(NEVERC_IR_TYPE_METADATA);
  auto I129 = Bridge.getIntegerType(129);
  ASSERT_TRUE(static_cast<bool>(Void));
  ASSERT_TRUE(static_cast<bool>(Float));
  ASSERT_TRUE(static_cast<bool>(Token));
  ASSERT_TRUE(static_cast<bool>(Metadata));
  ASSERT_TRUE(static_cast<bool>(I129));

  auto Pointer = Bridge.getPointerType(7);
  auto Array = Bridge.getArrayType(*I129, 3);
  auto FixedVector = Bridge.getVectorType(*Float, 4, false);
  auto ScalableVector = Bridge.getVectorType(*Float, 8, true);
  ASSERT_TRUE(static_cast<bool>(Pointer));
  ASSERT_TRUE(static_cast<bool>(Array));
  ASSERT_TRUE(static_cast<bool>(FixedVector));
  ASSERT_TRUE(static_cast<bool>(ScalableVector));

  const std::array<NevercIRTypeHandle, 2> Members = {*I129, *Pointer};
  auto Structure = Bridge.getStructType("Pair", Members, true);
  ASSERT_TRUE(static_cast<bool>(Structure));
  const std::array<NevercIRTypeHandle, 2> Parameters = {
      *Pointer, *ScalableVector};
  auto Function = Bridge.getFunctionType(*Void, Parameters, true);
  ASSERT_TRUE(static_cast<bool>(Function));

  const std::array<std::pair<NevercIRTypeHandle, NevercIRTypeKind>, 10>
      Expected = {{
          {*Void, NEVERC_IR_TYPE_VOID},
          {*Float, NEVERC_IR_TYPE_FLOAT},
          {*Token, NEVERC_IR_TYPE_TOKEN},
          {*Metadata, NEVERC_IR_TYPE_METADATA},
          {*I129, NEVERC_IR_TYPE_INTEGER},
          {*Pointer, NEVERC_IR_TYPE_POINTER},
          {*Array, NEVERC_IR_TYPE_ARRAY},
          {*FixedVector, NEVERC_IR_TYPE_FIXED_VECTOR},
          {*ScalableVector, NEVERC_IR_TYPE_SCALABLE_VECTOR},
          {*Function, NEVERC_IR_TYPE_FUNCTION},
      }};
  for (const auto &[Handle, ExpectedKind] : Expected) {
    NevercIRTypeKind Kind = NEVERC_IR_TYPE_UNKNOWN;
    EXPECT_EQ(Bridge.getTypeKind(Handle, &Kind).Code, NEVERC_STATUS_OK);
    EXPECT_EQ(Kind, ExpectedKind);
  }

  LLVMContext ForeignContext;
  auto Foreign =
      Bridge.wrapType(*Type::getInt32Ty(ForeignContext));
  EXPECT_FALSE(static_cast<bool>(Foreign));
  consumeError(Foreign.takeError());
}

TEST(PluginIRDataTest, CreatesConstantsAndRoundTripsTheirTextualIR) {
  IRDataScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  auto I1 = Bridge.getIntegerType(1);
  auto I32 = Bridge.getIntegerType(32);
  auto I129 = Bridge.getIntegerType(129);
  auto FP128 = Bridge.getPrimitiveType(NEVERC_IR_TYPE_FP128);
  auto Pointer = Bridge.getPointerType(0);
  ASSERT_TRUE(static_cast<bool>(I1));
  ASSERT_TRUE(static_cast<bool>(I32));
  ASSERT_TRUE(static_cast<bool>(I129));
  ASSERT_TRUE(static_cast<bool>(FP128));
  ASSERT_TRUE(static_cast<bool>(Pointer));

  const std::array<uint64_t, 3> WideWords = {
      UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
      UINT64_C(1)};
  auto Wide = Bridge.getIntegerConstant(*I129, WideWords);
  const std::array<uint64_t, 2> FloatWords = {
      UINT64_C(0x0123456789abcdef), UINT64_C(0x4000000000000000)};
  auto Quad = Bridge.getFloatingConstant(*FP128, FloatWords);
  auto NullPointer = Bridge.getNullConstant(*Pointer);
  auto Poison = Bridge.getPoisonConstant(*I32);
  auto Undef = Bridge.getUndefConstant(*I32);
  ASSERT_TRUE(static_cast<bool>(Wide));
  ASSERT_TRUE(static_cast<bool>(Quad));
  ASSERT_TRUE(static_cast<bool>(NullPointer));
  ASSERT_TRUE(static_cast<bool>(Poison));
  ASSERT_TRUE(static_cast<bool>(Undef));

  Value *Resolved = nullptr;
  ASSERT_EQ(Bridge.resolveValue(*Wide, &Resolved).Code, NEVERC_STATUS_OK);
  auto *WideConstant = dyn_cast<ConstantInt>(Resolved);
  ASSERT_NE(WideConstant, nullptr);
  EXPECT_EQ(WideConstant->getValue().getBitWidth(), 129U);
  EXPECT_EQ(WideConstant->getValue().getRawData()[0], WideWords[0]);
  EXPECT_EQ(WideConstant->getValue().getRawData()[1], WideWords[1]);
  EXPECT_EQ(WideConstant->getValue().getRawData()[2], WideWords[2]);

  auto ArrayType = Bridge.getArrayType(*I129, 3);
  ASSERT_TRUE(static_cast<bool>(ArrayType));
  auto One = Bridge.getIntegerConstant(*I129, ArrayRef<uint64_t>(WideWords).take_front(1));
  ASSERT_TRUE(static_cast<bool>(One));
  const std::array<NevercIRValueHandle, 3> Values = {*Wide, *One, *Wide};
  auto Aggregate = Bridge.getAggregateConstant(*ArrayType, Values);
  ASSERT_TRUE(static_cast<bool>(Aggregate));

  auto Two = Bridge.getIntegerConstant(*I129, std::array<uint64_t, 3>{2, 0, 0});
  ASSERT_TRUE(static_cast<bool>(Two));
  auto Sum =
      Bridge.getConstantBinaryExpression(NEVERC_IR_OPCODE_ADD, *One, *Two);
  ASSERT_TRUE(static_cast<bool>(Sum));

  Type *ResolvedI129 = nullptr;
  ASSERT_EQ(Bridge.resolveType(*I129, &ResolvedI129).Code, NEVERC_STATUS_OK);
  auto *Global = new GlobalVariable(
      Bridge.module(), ResolvedI129, true, GlobalValue::ExternalLinkage,
      cast<Constant>(Resolved), "wide");
  auto GlobalHandle = Bridge.wrapValue(*Global);
  ASSERT_TRUE(static_cast<bool>(GlobalHandle));
  auto Address = Bridge.getGlobalAddressConstant(*GlobalHandle);
  ASSERT_TRUE(static_cast<bool>(Address));
  EXPECT_EQ(Address->Owner, GlobalHandle->Owner);
  EXPECT_EQ(Address->Value, GlobalHandle->Value);
  auto AddressInteger = Bridge.getConstantCastExpression(
      NEVERC_IR_OPCODE_PTR_TO_INT, *Address, *I129);
  ASSERT_TRUE(static_cast<bool>(AddressInteger));
  Value *ResolvedAddressInteger = nullptr;
  ASSERT_EQ(Bridge.resolveValue(*AddressInteger, &ResolvedAddressInteger).Code,
            NEVERC_STATUS_OK);
  new GlobalVariable(Bridge.module(), ResolvedI129, true,
                     GlobalValue::InternalLinkage,
                     cast<Constant>(ResolvedAddressInteger), "address");

  Type *ResolvedArray = nullptr;
  Value *ResolvedAggregate = nullptr;
  ASSERT_EQ(Bridge.resolveType(*ArrayType, &ResolvedArray).Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Bridge.resolveValue(*Aggregate, &ResolvedAggregate).Code,
            NEVERC_STATUS_OK);
  new GlobalVariable(Bridge.module(), ResolvedArray, true,
                     GlobalValue::InternalLinkage,
                     cast<Constant>(ResolvedAggregate), "aggregate");
  Value *ResolvedQuad = nullptr;
  ASSERT_EQ(Bridge.resolveValue(*Quad, &ResolvedQuad).Code, NEVERC_STATUS_OK);
  new GlobalVariable(Bridge.module(), cast<Constant>(ResolvedQuad)->getType(),
                     true, GlobalValue::InternalLinkage,
                     cast<Constant>(ResolvedQuad), "quad");

  std::string Text;
  raw_string_ostream Stream(Text);
  Bridge.module().print(Stream, nullptr);
  Stream.flush();
  SmallVector<char, 0> Bitcode;
  raw_svector_ostream BitcodeStream(Bitcode);
  WriteBitcodeToFile(Bridge.module(), BitcodeStream);
  LLVMContext ParsedContext;
  auto Parsed = parseBitcodeFile(
      MemoryBufferRef(StringRef(Bitcode.data(), Bitcode.size()), "roundtrip"),
      ParsedContext);
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << errorMessage(Parsed.takeError());
  (*Parsed)->setModuleIdentifier(Bridge.module().getModuleIdentifier());
  std::string RoundTrippedText;
  raw_string_ostream RoundTrippedStream(RoundTrippedText);
  (*Parsed)->print(RoundTrippedStream, nullptr);
  RoundTrippedStream.flush();
  EXPECT_EQ(RoundTrippedText, Text);
  EXPECT_NE((*Parsed)->getGlobalVariable("wide"), nullptr);
  EXPECT_NE((*Parsed)->getGlobalVariable("aggregate", true), nullptr);
  EXPECT_NE((*Parsed)->getGlobalVariable("quad", true), nullptr);
  EXPECT_NE((*Parsed)->getGlobalVariable("address", true), nullptr);
}

TEST(PluginIRDataTest, CreatesMetadataAndReadsDebugLocations) {
  IRDataScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  const char EmbeddedBytes[] = {'N', '\0', 'C'};
  auto Text = Bridge.getMetadataString(
      StringRef(EmbeddedBytes, sizeof(EmbeddedBytes)));
  ASSERT_TRUE(static_cast<bool>(Text));
  auto ReadText = Bridge.getMetadataStringBytes(*Text);
  ASSERT_TRUE(static_cast<bool>(ReadText));
  EXPECT_EQ(ReadText->size(), sizeof(EmbeddedBytes));
  EXPECT_EQ(*ReadText, StringRef(EmbeddedBytes, sizeof(EmbeddedBytes)));

  const std::array<NevercIRMetadataHandle, 1> NodeOperands = {*Text};
  auto Node = Bridge.getMetadataNode(NodeOperands, true);
  ASSERT_TRUE(static_cast<bool>(Node));
  NevercIRMetadataKind Kind = NEVERC_IR_METADATA_UNKNOWN;
  EXPECT_EQ(Bridge.getMetadataKind(*Node, &Kind).Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Kind, NEVERC_IR_METADATA_NODE);
  uint64_t OperandCount = 0;
  EXPECT_EQ(Bridge.getMetadataOperandCount(*Node, &OperandCount).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(OperandCount, 1U);
  auto FirstOperand = Bridge.getMetadataOperand(*Node, 0);
  ASSERT_TRUE(static_cast<bool>(FirstOperand));
  EXPECT_EQ(FirstOperand->Owner, Text->Owner);
  EXPECT_EQ(FirstOperand->Value, Text->Value);

  auto Named = Bridge.getOrInsertNamedMetadata("neverc.test");
  ASSERT_TRUE(static_cast<bool>(Named));
  EXPECT_EQ(Bridge.appendNamedMetadata(*Named, *Node).Code, NEVERC_STATUS_OK);
  uint64_t NamedCount = 0;
  EXPECT_EQ(Bridge.getNamedMetadataOperandCount(*Named, &NamedCount).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(NamedCount, 1U);
  auto NamedOperand = Bridge.getNamedMetadataOperand(*Named, 0);
  ASSERT_TRUE(static_cast<bool>(NamedOperand));
  EXPECT_EQ(NamedOperand->Owner, Node->Owner);
  EXPECT_EQ(NamedOperand->Value, Node->Value);

  auto I32 = Bridge.getIntegerType(32);
  ASSERT_TRUE(static_cast<bool>(I32));
  auto Number =
      Bridge.getIntegerConstant(*I32, std::array<uint64_t, 1>{17});
  ASSERT_TRUE(static_cast<bool>(Number));
  auto ValueMetadata = Bridge.getValueAsMetadata(*Number);
  ASSERT_TRUE(static_cast<bool>(ValueMetadata));
  EXPECT_EQ(Bridge.getMetadataKind(*ValueMetadata, &Kind).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Kind, NEVERC_IR_METADATA_VALUE);
  auto MetadataValue = Bridge.getMetadataAsValue(*ValueMetadata);
  ASSERT_TRUE(static_cast<bool>(MetadataValue));
  Value *ResolvedMetadataValue = nullptr;
  ASSERT_EQ(Bridge.resolveValue(*MetadataValue, &ResolvedMetadataValue).Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(ResolvedMetadataValue->getType()->isMetadataTy());

  DIBuilder DebugBuilder(Bridge.module());
  DIFile *File = DebugBuilder.createFile("test.c", "/virtual");
  DebugBuilder.createCompileUnit(dwarf::DW_LANG_C, File, "NeverC", false, "",
                                 0);
  DISubroutineType *SubroutineType = DebugBuilder.createSubroutineType(
      DebugBuilder.getOrCreateTypeArray({}));
  DISubprogram *Subprogram = DebugBuilder.createFunction(
      File, "f", "f", File, 11, SubroutineType, 11, DINode::FlagZero,
      DISubprogram::SPFlagDefinition);
  DILocation *Location =
      DILocation::get(Bridge.context(), 42, 7, Subprogram, nullptr, true);
  auto LocationHandle = Bridge.wrapMetadata(*Location);
  ASSERT_TRUE(static_cast<bool>(LocationHandle));
  NevercIRDebugLocationInfo Info{};
  Info.Size = sizeof(Info);
  Info.Version = 1;
  EXPECT_EQ(Bridge.getDebugLocationInfo(*LocationHandle, &Info).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Line, 42U);
  EXPECT_EQ(Info.Column, 7U);
  EXPECT_EQ(Info.IsImplicitCode, 1U);
  EXPECT_NE(Info.Scope.Owner, 0U);
  EXPECT_EQ(Info.InlinedAt.Owner, 0U);
  DebugBuilder.finalize();
}

TEST(PluginIRDataTest, AppliesEveryAttributeFormAtEveryLocation) {
  IRDataScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  auto Pointer = Bridge.getPointerType(0);
  ASSERT_TRUE(static_cast<bool>(Pointer));
  const std::array<NevercIRTypeHandle, 1> Members = {*Pointer};
  auto Record = Bridge.getStructType("Record", Members, false);
  ASSERT_TRUE(static_cast<bool>(Record));
  const std::array<NevercIRTypeHandle, 1> Parameters = {*Pointer};
  auto Signature = Bridge.getFunctionType(*Pointer, Parameters, false);
  ASSERT_TRUE(static_cast<bool>(Signature));
  Type *ResolvedSignature = nullptr;
  ASSERT_EQ(Bridge.resolveType(*Signature, &ResolvedSignature).Code,
            NEVERC_STATUS_OK);
  Function *Target =
      Function::Create(cast<FunctionType>(ResolvedSignature),
                       GlobalValue::ExternalLinkage, "target", Bridge.module());
  auto TargetHandle = Bridge.wrapValue(*Target);
  ASSERT_TRUE(static_cast<bool>(TargetHandle));

  auto NoUnwind = Bridge.createEnumAttribute("nounwind");
  auto NoAlias = Bridge.createEnumAttribute("noalias");
  auto Alignment = Bridge.createIntegerAttribute("align", 16);
  auto ProbeStack =
      Bridge.createStringAttribute("probe-stack", "__neverc_probe");
  auto ByValue = Bridge.createTypeAttribute("byval", *Record);
  ASSERT_TRUE(static_cast<bool>(NoUnwind));
  ASSERT_TRUE(static_cast<bool>(NoAlias));
  ASSERT_TRUE(static_cast<bool>(Alignment));
  ASSERT_TRUE(static_cast<bool>(ProbeStack));
  ASSERT_TRUE(static_cast<bool>(ByValue));

  EXPECT_EQ(Bridge.addFunctionAttribute(
                *TargetHandle, NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0,
                *NoUnwind)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.addFunctionAttribute(
                *TargetHandle, NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0,
                *ProbeStack)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.addFunctionAttribute(
                *TargetHandle, NEVERC_IR_ATTRIBUTE_LOCATION_RETURN, 0,
                *NoAlias)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.addFunctionAttribute(
                *TargetHandle, NEVERC_IR_ATTRIBUTE_LOCATION_PARAMETER, 0,
                *Alignment)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.addFunctionAttribute(
                *TargetHandle, NEVERC_IR_ATTRIBUTE_LOCATION_PARAMETER, 0,
                *ByValue)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_TRUE(Target->hasFnAttribute(Attribute::NoUnwind));
  EXPECT_TRUE(Target->hasFnAttribute("probe-stack"));
  EXPECT_TRUE(Target->getAttributes().hasRetAttr(Attribute::NoAlias));
  EXPECT_EQ(Target->getParamAlign(0), Align(16));
  EXPECT_EQ(Target->getParamByValType(0), cast<StructType>(
                                              [&] {
                                                Type *TypeValue = nullptr;
                                                EXPECT_EQ(
                                                    Bridge.resolveType(
                                                        *Record, &TypeValue)
                                                        .Code,
                                                    NEVERC_STATUS_OK);
                                                return TypeValue;
                                              }()));

  NevercIRAttributeValueKind ValueKind = 0;
  EXPECT_EQ(Bridge.getAttributeValueKind(*NoUnwind, &ValueKind).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ValueKind, NEVERC_IR_ATTRIBUTE_ENUM);
  EXPECT_EQ(*Bridge.getAttributeKindName(*Alignment), "align");
  EXPECT_EQ(*Bridge.getAttributeIntegerValue(*Alignment), 16U);
  EXPECT_EQ(*Bridge.getAttributeStringValue(*ProbeStack), "__neverc_probe");
  auto ByValueType = Bridge.getAttributeTypeValue(*ByValue);
  ASSERT_TRUE(static_cast<bool>(ByValueType));
  EXPECT_EQ(ByValueType->Owner, Record->Owner);
  EXPECT_EQ(ByValueType->Value, Record->Value);
}

TEST(PluginIRDataTest, PublishesUsableCoreCABI) {
  IRDataScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const NevercIRCoreAPI &API = Scope.bridge().coreAPI();
  EXPECT_EQ(API.Header.StructSize, sizeof(API));
  EXPECT_EQ(API.Header.Major, NEVERC_IR_CORE_API_MAJOR);
  EXPECT_EQ(API.Header.Minor, NEVERC_IR_CORE_API_MINOR);

  NevercIRTypeHandle I129{};
  EXPECT_EQ(API.GetIntegerType(API.Context, Scope.taskHandle(), 129, &I129).Code,
            NEVERC_STATUS_OK);
  const uint64_t Words[] = {1, 2, 1};
  NevercIRValueHandle Integer{};
  EXPECT_EQ(API.CreateIntegerConstant(API.Context, Scope.taskHandle(), I129,
                                      Words, 3, &Integer)
                .Code,
            NEVERC_STATUS_OK);

  const char Bytes[] = {'A', '\0', 'B'};
  NevercIRMetadataHandle Metadata{};
  EXPECT_EQ(API.CreateMetadataString(
                API.Context, Scope.taskHandle(),
                NevercStringView{Bytes, sizeof(Bytes)}, &Metadata)
                .Code,
            NEVERC_STATUS_OK);
  NevercStringView ReadBytes{};
  EXPECT_EQ(API.GetMetadataStringBytes(API.Context, Scope.taskHandle(),
                                       Metadata, &ReadBytes)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ReadBytes.Length, sizeof(Bytes));
  EXPECT_EQ(StringRef(ReadBytes.Data, ReadBytes.Length),
            StringRef(Bytes, sizeof(Bytes)));

  NevercIRAttributeHandle Attribute{};
  EXPECT_EQ(API.CreateIntegerAttribute(
                API.Context, Scope.taskHandle(), NevercStringView{"align", 5},
                32, &Attribute)
                .Code,
            NEVERC_STATUS_OK);
  uint64_t Alignment = 0;
  EXPECT_EQ(API.GetAttributeIntegerValue(
                API.Context, Scope.taskHandle(), Attribute, &Alignment)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Alignment, 32U);

  NevercTaskHandle WrongTask = Scope.taskHandle();
  ++WrongTask.Value;
  NevercIRTypeHandle Rejected{};
  EXPECT_EQ(API.GetIntegerType(API.Context, WrongTask, 32, &Rejected).Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(API.GetIntegerType(API.Context, Scope.taskHandle(), 0, &Rejected)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
}

} // namespace
