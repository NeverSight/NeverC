#include "NeverCTestFixture.h"
#include "TranslateIR.h"
#include <algorithm>

using namespace neverc::translate;

namespace {
const SourceLocation InputLoc{"input.cpp", 1, 1};
Type intType() { return {TypeKind::Int, {}}; }
Type uintType() { return {TypeKind::UInt, {}}; }
Type boolType() { return {TypeKind::Bool, {}}; }
Type integerType(unsigned Bits, bool Unsigned = false) {
  Type T{Unsigned ? TypeKind::UInt : TypeKind::Int, {}};
  T.IntegerBits = Bits == 32 ? 0 : Bits;
  return T;
}
Type pointerType(Type Pointee, bool Const = false) {
  return {TypeKind::Pointer, {}, {std::move(Pointee)}, Const};
}
Type arrayType(Type Element, uint32_t Count) {
  return {TypeKind::Array, {}, {std::move(Element)}, false, Count};
}
Expr pointerExpr(ExprKind Kind, Type T, std::vector<Expr> Args = {}) {
  Expr E;
  E.Kind = Kind;
  E.ValueType = std::move(T);
  E.Loc = InputLoc;
  E.Args = std::move(Args);
  return E;
}
Expr literal(std::string Value, Type T = intType()) {
  Expr E;
  E.ValueType = std::move(T);
  E.Loc = InputLoc;
  E.Integer = std::move(Value);
  return E;
}
Expr variable(std::string Name, Type T = intType()) {
  Expr E;
  E.Kind = ExprKind::Var;
  E.ValueType = std::move(T);
  E.Loc = InputLoc;
  E.Name = std::move(Name);
  return E;
}
Expr binary(BinaryOperator Op, Expr A, Expr B, Type T = intType()) {
  Expr E;
  E.Kind = ExprKind::Binary;
  E.ValueType = std::move(T);
  E.Loc = InputLoc;
  E.BinaryOp = Op;
  E.Args = {std::move(A), std::move(B)};
  return E;
}
Instruction label(std::string Name = "nct_entry") {
  Instruction I;
  I.Op = InstructionKind::Label;
  I.Loc = InputLoc;
  I.Label = std::move(Name);
  return I;
}
Instruction ret(Expr E) {
  Instruction I;
  I.Op = InstructionKind::Return;
  I.Loc = InputLoc;
  I.Value = std::move(E);
  return I;
}
CarrierLayout x64CarrierLayout() {
  CarrierLayout L;
  // Explicit x86_64 Linux ABI fixture, independent of module evidence.
  L.Carriers = {{{8, 8}, {8, 8}, {8, 8}, {16, 16}, {16, 16},
                 {32, 32}, {32, 32}, {64, 64}, {64, 64}, {64, 64}}};
  return L;
}
std::string carrierLayoutWire() {
  auto L = x64CarrierLayout();
  std::string JSON = "{\"char_bits\":8";
  for (size_t I = 0; I < CarrierNames.size(); ++I)
    JSON += ",\"" + std::string(CarrierNames[I]) + "\":{\"size_bits\":" +
            std::to_string(L.Carriers[I].SizeBits) + ",\"abi_align_bits\":" +
            std::to_string(L.Carriers[I].ABIAlignBits) + "}";
  return JSON + "}";
}
Module module(bool CoreV2 = false) {
  Module M;
  M.Profile = CoreV2 ? "cpp-core-v2" : "cpp-core-v1";
  M.Frontend = {CppFrontendName, CppFrontendVersion, "cpp-frontend-1"};
  M.Target = {"x86_64-unknown-linux-gnu", 32, 64, true};
  if (CoreV2)
    M.Target.Carriers = x64CarrierLayout();
  M.Dependencies = {{"input.cpp", std::string(64, 'a')}};
  Function F;
  F.Name = "sample";
  F.Result = intType();
  F.CExport = true;
  F.Loc = InputLoc;
  F.Body = {label(), ret(literal("42"))};
  M.Functions.push_back(std::move(F));
  return M;
}
VerificationContext context(const Module &M) {
  VerificationContext C{M.Profile, M.Target.Triple, M.Target.IntBits,
                        M.Target.PointerBits, M.Target.LittleEndian};
  if (M.Profile == "cpp-core-v2") {
    C.ExpectedCarrierLayout = x64CarrierLayout();
    C.ExpectedPtrDiffBits = 64;
  }
  return C;
}
void invalid(const Module &M, const std::string &Reason = {}) {
  Diagnostics D;
  EXPECT_FALSE(verifyModule(M, context(M), D));
  ASSERT_FALSE(D.empty());
  if (!Reason.empty()) {
    bool Found = std::any_of(D.begin(), D.end(), [&](const Diagnostic &E) {
      return E.Reason.find(Reason) != std::string::npos;
    });
    EXPECT_TRUE(Found) << D.front().Reason;
  }
  EmittedSource Output{"unchanged", {}};
  EXPECT_FALSE(emitNC(M, context(M), Output, D));
  EXPECT_EQ(Output.Text, "unchanged");
}
std::string wireModule(bool CoreV2 = false) {
  std::string JSON = R"json({
  "protocol": 1, "profile": "cpp-core-v1",
  "frontend": {"name": "neverc-cpp-frontend", "version": "20.1.8", "build": "cpp-frontend-1"},
  "target": {"triple": "x86_64-unknown-linux-gnu", "int_bits": 32, "pointer_bits": 64, "little_endian": true},
  "dependencies": [{"path": "input.cpp", "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}],
  "records": [], "globals": [], "mappings": [], "diagnostics": [],
  "functions": [{"name": "sample", "result": "bool", "internal": false, "c_export": true,
    "params": [], "locals": [], "loc": {"file": "input.cpp", "line": 2, "column": 1},
    "body": [
      {"op": "label", "label": "nct_entry", "loc": {"file": "input.cpp", "line": 2, "column": 1}},
      {"op": "return", "loc": {"file": "input.cpp", "line": 2, "column": 2},
       "value": {"kind": "literal", "type": "bool", "value": true,
                 "loc": {"file": "input.cpp", "line": 2, "column": 9}}}
    ]}]
})json";
  if (CoreV2) {
    JSON.replace(JSON.find("cpp-core-v1"), std::string("cpp-core-v1").size(),
                 "cpp-core-v2");
    auto At = JSON.find("\"little_endian\": true") +
              std::string("\"little_endian\": true").size();
    JSON.insert(At, ", \"carrier_layout\":" + carrierLayoutWire());
  }
  return JSON;
}
void replaceOnce(std::string &S, const std::string &Old,
                 const std::string &New) {
  auto At = S.find(Old);
  ASSERT_NE(At, std::string::npos);
  S.replace(At, Old.size(), New);
}
} // namespace

TEST(TranslateIR, ParsesTypedModuleAndDerivesExports) {
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(wireModule(), M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  ASSERT_EQ(M.Exports.size(), 1u);
  EXPECT_EQ(M.Exports.front().Name, "sample");
  EXPECT_EQ(M.Exports.front().Result, "bool");
  EXPECT_TRUE(M.Functions.front().Body.back().Value->Boolean);
}

TEST(TranslateIR, CoreV2ParsesVerifiesAndEmitsWithMatchingProfile) {
  auto JSON = wireModule(true);
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(JSON, M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("profile cpp-core-v2"), std::string::npos);
  EXPECT_EQ(M.Exports.front().Result, "bool");
}

TEST(TranslateIR, CoreV2IntegerWidthsParseCanonicalBoundaryValues) {
  struct ValueCase { const char *Type, *Value, *Emitted; };
  for (const auto &Case : std::vector<ValueCase>{
           {"i8", "-128", "((signed char)(-128))"},
           {"u8", "255", "((unsigned char)(255))"},
           {"i16", "-32768", "((short)(-32768))"},
           {"u16", "65535", "((unsigned short)(65535))"},
           {"i64", "-9223372036854775808", "(-9223372036854775807ll - 1ll)"},
           {"i64", "9223372036854775807", "(9223372036854775807ll)"},
           {"u64", "18446744073709551615", "18446744073709551615ull"}}) {
    SCOPED_TRACE(Case.Type);
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", std::string("\"result\":\"") + Case.Type + "\"");
    replaceOnce(JSON, "\"type\": \"bool\", \"value\": true",
                std::string("\"type\":\"") + Case.Type + "\",\"value\":\"" + Case.Value + "\"");
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(JSON, M, D));
    EXPECT_EQ(typeName(M.Functions[0].Result), Case.Type);
    EmittedSource Output;
    ASSERT_TRUE(emitNC(M, context(M), Output, D));
    EXPECT_NE(Output.Text.find(Case.Emitted), std::string::npos) << Output.Text;
  }
  for (const auto *Bad : {"i32", "u32", "i08", "u128", "i0", "u65"}) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", std::string("\"result\":\"") + Bad + "\"");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D));
  }
}

TEST(TranslateIR, CoreV2IntegerWidthsRejectInvalidRangesAndUnpromotedOperations) {
  for (const auto &Pair : std::vector<std::pair<Type, std::string>>{
           {integerType(8), "128"}, {integerType(8), "-129"},
           {integerType(8, true), "256"}, {integerType(8, true), "-1"},
           {integerType(16), "32768"}, {integerType(16), "-32769"},
           {integerType(16, true), "65536"},
           {integerType(64), "9223372036854775808"},
           {integerType(64), "-9223372036854775809"},
           {integerType(64, true), "18446744073709551616"},
           {integerType(64), "-0"}, {integerType(64), "01"}}) {
    auto M = module(true);
    M.Functions[0].Result = Pair.first;
    M.Functions[0].Body.back() = ret(literal(Pair.second, Pair.first));
    invalid(M, "literal value or range");
  }
  auto M = module(true);
  auto Narrow = integerType(8, true);
  M.Functions[0].Result = Narrow;
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add,
      literal("1", Narrow), literal("2", Narrow), Narrow));
  invalid(M, "source integer promotions");
  auto Unary = pointerExpr(ExprKind::Unary, Narrow, {literal("1", Narrow)});
  Unary.UnaryOp = UnaryOperator::Plus;
  M.Functions[0].Body.back() = ret(std::move(Unary));
  invalid(M, "promoted");
  auto ElementPointer = pointerType(Narrow);
  M.Functions[0].Params = {{"nct_bytes", ElementPointer, InputLoc}};
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Index, Narrow,
      {variable("nct_bytes", ElementPointer), literal("0", Narrow)}));
  invalid(M, "promoted integer");
  M.Functions[0].Params.clear();
  M.Functions[0].Result = intType();
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add,
      pointerExpr(ExprKind::Cast, intType(), {literal("1", Narrow)}),
      pointerExpr(ExprKind::Cast, intType(), {literal("2", Narrow)})));
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  auto From = pointerType(integerType(8));
  auto To = pointerType(integerType(16));
  M.Functions[0].Params = {{"nct_p", From, InputLoc}};
  M.Functions[0].Result = To;
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Cast, To,
      {variable("nct_p", From)}));
  invalid(M, "pointer conversion");
}

TEST(TranslateIR, CoreV2IntegerWidthsCannotBypassTypeOrProfileRules) {
  for (auto Bad : {boolType(), pointerType(intType()),
                   arrayType(intType(), 2), intType()}) {
    Bad.IntegerBits = Bad.Kind == TypeKind::Int ? 32 : 8;
    auto M = module(true);
    M.Functions[0].Result = Bad;
    invalid(M, "require core v2 scalar types");
  }
  auto M = module();
  M.Functions[0].Result = integerType(64);
  invalid(M, "require core v2 scalar types");
}

TEST(TranslateIR, CoreV2EmitsSeparateSignedConversionAndShiftWidths) {
  auto M = module(true);
  auto U64 = integerType(64, true), I64 = integerType(64);
  for (unsigned Bits : {8, 16, 32, 64}) {
    Function F = M.Functions[0];
    F.Name = "convert" + std::to_string(Bits);
    F.Result = integerType(Bits);
    F.Params = {{"nct_input", U64, InputLoc}};
    F.Body.back() = ret(pointerExpr(ExprKind::Cast, F.Result,
                                    {variable("nct_input", U64)}));
    M.Functions.push_back(std::move(F));
  }
  Function Shift = M.Functions[0];
  Shift.Name = "shift";
  Shift.Result = I64;
  Shift.Params = {{"nct_input", I64, InputLoc}};
  Shift.Body.back() = ret(binary(BinaryOperator::ShiftRight,
      variable("nct_input", I64), literal("63"), I64));
  M.Functions.push_back(Shift);
  Shift.Name = "leftshift";
  Shift.Body.back().Value->BinaryOp = BinaryOperator::ShiftLeft;
  M.Functions.push_back(Shift);
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  for (unsigned Bits : {8, 16, 32, 64})
    EXPECT_NE(Output.Text.find("nct_emit_u" + std::to_string(Bits) +
                              "_to_i" + std::to_string(Bits)), std::string::npos);
  EXPECT_NE(Output.Text.find("static long long nct_emit_i64_shr(long long"),
            std::string::npos);
  EXPECT_NE(Output.Text.find("18446744073709551615ull - nct_emit_u"),
            std::string::npos);
  EXPECT_NE(Output.Text.find("nct_emit_u64_to_i64(((unsigned long long)"),
            std::string::npos);
}

TEST(TranslateIR, CoreV2RequiresIndependentCarrierLayout) {
  auto M = module(true);
  auto C = context(M);
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, C, Output, D));
  EXPECT_NE(Output.Text.find("alignof(unsigned long long) * __CHAR_BIT__ == 64"),
            std::string::npos);
  EXPECT_NE(Output.Text.find("sizeof(void *) * __CHAR_BIT__ == 64"),
            std::string::npos);
  C.ExpectedCarrierLayout.reset();
  EXPECT_FALSE(verifyModule(M, C, D));
  M.Target.Carriers.reset();
  invalid(M, "independent carrier layout");
  M = module(true);
  M.Target.Carriers->Carriers[7].ABIAlignBits = 32;
  invalid(M, "layouts disagree");
  M = module(true);
  M.Target.Carriers->Carriers[9].SizeBits = 32;
  invalid(M, "layouts disagree");
  // An equally malformed synthetic context must not admit an invalid model.
  for (auto Bad : {StorageLayout{64, 24}, StorageLayout{128, 64},
                   StorageLayout{64, 0}, StorageLayout{64, 128}}) {
    M = module(true);
    C = context(M);
    M.Target.Carriers->Carriers[7] = Bad;
    C.ExpectedCarrierLayout->Carriers[7] = Bad;
    EXPECT_FALSE(verifyModule(M, C, D));
  }
  M = module(true);
  M.Profile = "cpp-core-v1";
  invalid(M, "requires core v2");
}

TEST(TranslateIR, CoreV2LayoutWireRequiresExactFields) {
  auto Missing = wireModule();
  replaceOnce(Missing, "cpp-core-v1", "cpp-core-v2");
  Module M;
  Diagnostics D;
  EXPECT_FALSE(parseModule(Missing, M, D));
  for (const auto &Pair : std::vector<std::pair<std::string, std::string>>{
           {"\"char_bits\":8", "\"char_bits\":8,\"unknown\":1"},
           {"\"i8\":", "\"i08\":"},
           {"\"abi_align_bits\":64", "\"abi_align_bits\":64,\"extra\":0"},
           {"\"size_bits\":64", "\"size_bits\":-1"},
           {"\"size_bits\":64", "\"size_bits\":4294967296"},
           {"cpp-core-v2", "cpp-core-v1"}}) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, Pair.first, Pair.second);
    EXPECT_FALSE(parseModule(JSON, M, D)) << JSON;
  }
  auto JSON = wireModule(true);
  replaceOnce(JSON, "\"abi_align_bits\":64", "\"abi_align_bits\":32");
  ASSERT_TRUE(parseModule(JSON, M, D));
  invalid(M, "layouts disagree");
}

TEST(TranslateIR, CoreV2RecordLayoutChecksOffsetsPaddingAndNestedStorage) {
  auto M = module(true);
  Type Inner{TypeKind::Record, "nct_inner"};
  M.Records = {
      {"nct_inner", {{"flag", boolType()}, {"value", intType()},
                      {"tail", boolType()}}, InputLoc,
       RecordLayout{{96, 32}, {0, 32, 64}}},
      {"nct_outer", {{"flag", boolType()}, {"items", arrayType(Inner, 2)},
                      {"pointer", pointerType(Inner)}}, InputLoc,
       RecordLayout{{320, 64}, {0, 32, 256}}}};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("sizeof(nct_outer) * __CHAR_BIT__ == 320"),
            std::string::npos);
  EXPECT_NE(Output.Text.find("__builtin_offsetof(nct_outer, pointer) * "
                            "__CHAR_BIT__ == 256"), std::string::npos);
  auto Base = M;
  M.Records[0].Layout.reset();
  invalid(M, "Missing record layout");
  M = Base;
  M.Records[0].Layout->FieldOffsetsBits.pop_back();
  invalid(M, "mismatched field offsets");
  M = Base;
  M.Records[1].Layout->FieldOffsetsBits.back() = 224;
  invalid(M, "field offset disagrees");
  M = Base;
  M.Records[0].Layout->Storage.SizeBits = 72;
  invalid(M, "size or alignment disagrees");
  M = Base;
  M.Records[0].Layout->Storage.ABIAlignBits = 8;
  invalid(M, "size or alignment disagrees");
  M = Base;
  M.Records[0].Layout->Storage.SizeBits = UINT32_MAX;
  invalid(M, "size or alignment disagrees");
  M = module();
  M.Records.push_back(Base.Records[0]);
  invalid(M, "requires core v2");
}

TEST(TranslateIR, CoreV2EmptyRecordsHaveOneByteStorageWithoutSourceFields) {
  auto M = module(true);
  Type Empty{TypeKind::Record, "nct_empty"};
  M.Records = {
      {"nct_empty", {}, InputLoc, RecordLayout{{8, 8}, {}}},
      {"nct_outer", {{"first", Empty}, {"items", arrayType(Empty, 2)},
                      {"value", intType()}}, InputLoc,
       RecordLayout{{64, 32}, {0, 8, 32}}}};
  M.Functions[0].Locals.push_back({"nct_object", Empty, InputLoc});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = variable("nct_object", Empty);
  Assign.Value = pointerExpr(ExprKind::Aggregate, Empty);
  M.Functions[0].Body = {label(), Assign, ret(literal("0"))};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  const std::string Carrier = "unsigned char nct_emit_empty_storage;";
  const auto At = Output.Text.find(Carrier);
  ASSERT_NE(At, std::string::npos);
  EXPECT_EQ(Output.Text.find(Carrier, At + Carrier.size()), std::string::npos);
  EXPECT_NE(Output.Text.find("sizeof(nct_empty) * __CHAR_BIT__ == 8"), std::string::npos);
  EXPECT_NE(Output.Text.find("alignof(nct_empty) * __CHAR_BIT__ == 8"), std::string::npos);
  EXPECT_NE(Output.Text.find("__builtin_offsetof(nct_outer, items) * __CHAR_BIT__ == 8"), std::string::npos);
  EXPECT_NE(Output.Text.find("(nct_empty){}"), std::string::npos);
  EXPECT_EQ(Output.Text.find("__builtin_offsetof(nct_empty,"), std::string::npos);
  EXPECT_TRUE(M.Records[0].Fields.empty());
  EXPECT_TRUE(M.Records[0].Layout->FieldOffsetsBits.empty());
}

TEST(TranslateIR, CoreV2EmptyRecordLayoutRequiresExactIndependentEvidence) {
  auto Base = module(true);
  Base.Records.push_back({"nct_empty", {}, InputLoc, RecordLayout{{8, 8}, {}}});
  for (auto Bad : {StorageLayout{0, 8}, StorageLayout{16, 8},
                   StorageLayout{8, 16}, StorageLayout{8, 0}}) {
    auto M = Base;
    M.Records[0].Layout->Storage = Bad;
    invalid(M, "size or alignment disagrees");
  }
  auto M = Base;
  M.Records[0].Layout.reset();
  invalid(M, "Missing record layout");
  M = Base;
  M.Records[0].Layout->FieldOffsetsBits = {0};
  invalid(M, "mismatched field offsets");
  M = module();
  M.Records.push_back({"nct_empty", {}, InputLoc});
  invalid(M, "Empty records require core v2");
}

TEST(TranslateIR, CoreV2EmptyRecordCarrierIsNotAProtocolMemberOrInitializer) {
  auto M = module(true);
  Type Empty{TypeKind::Record, "nct_empty"};
  M.Records.push_back({"nct_empty", {}, InputLoc, RecordLayout{{8, 8}, {}}});
  M.Functions[0].Locals.push_back({"nct_object", Empty, InputLoc});
  auto Member = pointerExpr(ExprKind::Member, integerType(8, true),
                             {variable("nct_object", Empty)});
  Member.Name = "nct_emit_empty_storage";
  M.Functions[0].Result = integerType(8, true);
  M.Functions[0].Body.back() = ret(Member);
  invalid(M, "Unknown member");
  M.Functions[0].Result = intType();
  M.Functions[0].Body.back() = ret(literal("0"));
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = variable("nct_object", Empty);
  Assign.Value = pointerExpr(ExprKind::Aggregate, Empty, {literal("0")});
  M.Functions[0].Body.insert(M.Functions[0].Body.begin() + 1, Assign);
  invalid(M, "incorrect operand count");
}

TEST(TranslateIR, CoreV2EmptyElementsConsumeArrayStorageBudget) {
  auto M = module(true);
  Type Empty{TypeKind::Record, "nct_empty"};
  M.Records.push_back({"nct_empty", {}, InputLoc, RecordLayout{{8, 8}, {}}});
  M.Functions[0].Locals.push_back({"nct_items", arrayType(Empty, 65536), InputLoc});
  Diagnostics D;
  ASSERT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Locals[0].ValueType = arrayType(arrayType(Empty, 512), 512);
  invalid(M, "Array storage expansion");
  M = module(true);
  M.Records.push_back({"nct_cycle", {{"self", {TypeKind::Record, "nct_cycle"}}},
                        InputLoc, RecordLayout{{8, 8}, {0}}});
  invalid(M, "forward/cyclic");
}

TEST(TranslateIR, CoreV2EmptyRecordWireRetainsEmptyOffsetAndFieldArrays) {
  const std::string Record = R"json({"id":"nct_empty",
    "loc":{"file":"input.cpp","line":1,"column":1},"fields":[],
    "layout":{"size_bits":8,"abi_align_bits":8,"field_offsets_bits":[]}})json";
  auto Base = wireModule(true);
  replaceOnce(Base, "\"records\": []", "\"records\": [" + Record + "]");
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(Base, M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  ASSERT_EQ(M.Records.size(), 1u);
  EXPECT_TRUE(M.Records[0].Fields.empty());
  EXPECT_TRUE(M.Records[0].Layout->FieldOffsetsBits.empty());
  for (const auto &Change : std::vector<std::pair<std::string, std::string>>{
           {"\"size_bits\":8", "\"size_bits\":16"},
           {"\"abi_align_bits\":8", "\"abi_align_bits\":16"},
           {"\"field_offsets_bits\":[]", "\"field_offsets_bits\":[0]"}}) {
    auto JSON = Base;
    replaceOnce(JSON, Change.first, Change.second);
    ASSERT_TRUE(parseModule(JSON, M, D));
    invalid(M);
  }
  auto JSON = Base;
  replaceOnce(JSON, "\"layout\":", "\"missing\":");
  EXPECT_FALSE(parseModule(JSON, M, D));
}

TEST(TranslateIR, CoreV2RecordLayoutWireRejectsMissingAndMalformedEvidence) {
  const std::string Record = R"json({"id":"nct_record",
    "loc":{"file":"input.cpp","line":1,"column":1},
    "fields":[{"name":"value","type":"int"}],
    "layout":{"size_bits":32,"abi_align_bits":32,"field_offsets_bits":[0]}})json";
  auto Base = wireModule(true);
  replaceOnce(Base, "\"records\": []", "\"records\": [" + Record + "]");
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(Base, M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  for (const auto &Pair : std::vector<std::pair<std::string, std::string>>{
           {"\"layout\":", "\"missing\":"},
           {"\"field_offsets_bits\":[0]", "\"field_offsets_bits\":[-1]"},
           {"\"field_offsets_bits\":[0]", "\"field_offsets_bits\":[4294967296]"},
           {"\"field_offsets_bits\":[0]", "\"field_offsets_bits\":[0],\"extra\":1"}}) {
    auto JSON = Base;
    replaceOnce(JSON, Pair.first, Pair.second);
    EXPECT_FALSE(parseModule(JSON, M, D));
  }
  auto JSON = Base;
  replaceOnce(JSON, "\"field_offsets_bits\":[0]", "\"field_offsets_bits\":[8]");
  ASSERT_TRUE(parseModule(JSON, M, D));
  invalid(M, "field offset disagrees");
  JSON = wireModule();
  replaceOnce(JSON, "\"records\": []", "\"records\": [" + Record + "]");
  EXPECT_FALSE(parseModule(JSON, M, D));
}

TEST(TranslateIR, CoreV2CannotSubstituteForAnotherRequestedProfile) {
  for (const auto &Pair :
       std::vector<std::pair<std::string, std::string>>{
           {"cpp-core-v1", "cpp-core-v2"},
           {"cpp-core-v2", "cpp-core-v1"},
           {"cpp-core-v2", "cpp-project-v1"},
           {"cpp-core-v2", "cpp-math-v1"},
           {"cpp-core-v3", "cpp-core-v3"}}) {
    SCOPED_TRACE(Pair.first + " -> " + Pair.second);
    Module M = module();
    M.Profile = Pair.first;
    auto C = context(M);
    C.Profile = Pair.second;
    Diagnostics D;
    EXPECT_FALSE(verifyModule(M, C, D));
    ASSERT_FALSE(D.empty());
    EXPECT_EQ(D.front().Code, "TR0003");
    EmittedSource Output{"unchanged", {}};
    EXPECT_FALSE(emitNC(M, C, Output, D));
    EXPECT_EQ(Output.Text, "unchanged");
  }
}

TEST(TranslateIR, CoreV2ArrayPointerWireAndDeclarators) {
  for (const auto &Pair : std::vector<std::pair<std::string, std::string>>{
           {"ptr:arr:3:int", "int (* sample(void))[3]"},
           {"cptr:arr:3:int", "const int (* sample(void))[3]"},
           {"cptr:arr:3:ptr:int", "int *const (* sample(void))[3]"}}) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + Pair.first + "\"");
    replaceOnce(JSON, "\"kind\": \"literal\", \"type\": \"bool\", \"value\": true,",
                "\"kind\": \"null\", \"type\": \"" + Pair.first + "\",");
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(JSON, M, D));
    EmittedSource Output;
    ASSERT_TRUE(emitNC(M, context(M), Output, D));
    EXPECT_NE(Output.Text.find(Pair.second), std::string::npos) << Output.Text;
  }
  for (const auto *Spelling : {"arr:0:int", "arr:03:int", "arr:-1:int", "arr:65537:int"}) {
    auto JSON = wireModule();
    replaceOnce(JSON, "\"result\": \"bool\"", std::string("\"result\": \"ptr:") + Spelling + "\"");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D));
  }
}

TEST(TranslateIR, CoreV2ArraysAreStorageAndInitializerSubtrees) {
  auto M = module(true);
  auto Array = arrayType(intType(), 2);
  Type Record{TypeKind::Record, "nct_box"};
  M.Records.push_back({"nct_box", {{"values", Array}}, InputLoc,
                       RecordLayout{{64, 32}, {0}}});
  M.Functions[0].Locals.push_back({"nct_object", Record, InputLoc});
  Expr Values = pointerExpr(ExprKind::Aggregate, Array, {literal("1"), literal("2")});
  Expr Whole = pointerExpr(ExprKind::Aggregate, Record, {Values});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = variable("nct_object", Record);
  Assign.Value = Whole;
  M.Functions[0].Body = {label(), Assign, ret(literal("0"))};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("int values[2];"), std::string::npos);
  EXPECT_NE(Output.Text.find("(nct_box){{(1), (2)}}"), std::string::npos);
  M.Functions[0].Locals.push_back({"nct_array", Array, InputLoc});
  M.Functions[0].Body[1].Value->Args[0] = variable("nct_array", Array);
  invalid(M, "Array initializer children");
  auto Member = pointerExpr(ExprKind::Member, Array, {variable("nct_object", Record)});
  Member.Name = "values";
  M.Functions[0].Body[1].Value->Args[0] = Member;
  invalid(M, "Array initializer children");
  M.Functions[0].Body[1].Target = variable("nct_array", Array);
  M.Functions[0].Body[1].Value = variable("nct_array", Array);
  invalid(M, "equally typed");
  M.Functions[0].Body[1].Value = Values;
  invalid(M, "initializer subtree");
  M.Functions[0].Body = {label(), ret(variable("nct_array", Array))};
  M.Functions[0].Result = Array;
  invalid(M, "return arrays");
  M.Functions[0].Result = intType();
  M.Functions[0].Body.back() = ret(literal("0"));
  M.Functions[0].Params.push_back({"nct_param", Array, InputLoc});
  invalid(M, "scalar parameter");
  M.Functions[0].CExport = false;
  M.Functions[0].Name = "nct_sample";
  invalid(M, "adjusted pointer type");
}

TEST(TranslateIR, CoreV2ArrayDecayAndIndexRetainConstStorage) {
  auto M = module(true);
  auto Array = arrayType(intType(), 2);
  Type Record{TypeKind::Record, "nct_box"};
  M.Records.push_back({"nct_box", {{"values", Array}}, InputLoc,
                       RecordLayout{{64, 32}, {0}}});
  auto View = pointerType(Record, true);
  M.Functions[0].Params.push_back({"nct_view", View, InputLoc});
  auto Field = pointerExpr(ExprKind::Member, Array,
      {pointerExpr(ExprKind::Dereference, Record, {variable("nct_view", View)})});
  Field.Name = "values";
  auto Readonly = pointerType(intType(), true);
  auto Decay = pointerExpr(ExprKind::ArrayDecay, Readonly, {Field});
  M.Functions[0].Result = Readonly;
  M.Functions[0].Body.back() = ret(Decay);
  Diagnostics D;
  ASSERT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Result = pointerType(intType());
  M.Functions[0].Body.back().Value->ValueType = pointerType(intType());
  invalid(M, "Const pointee");
  auto Index = pointerExpr(ExprKind::Index, intType(), {Decay, literal("1")});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Index;
  Assign.Value = literal("9");
  M.Functions[0].Result = intType();
  M.Functions[0].Body = {label(), Assign, ret(Index)};
  invalid(M, "Const pointee");
  M.Functions[0].Body = {label(), ret(Index)};
  D.clear();
  EXPECT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Body.back().Value->ValueType = boolType();
  M.Functions[0].Result = boolType();
  invalid(M, "Index requires");
}

TEST(TranslateIR, CoreV2ArrayElementsNeedCompletenessAndBoundedStorage) {
  auto M = module(true);
  Type Later{TypeKind::Record, "nct_later"};
  M.Records = {{"nct_first", {{"items", arrayType(pointerType(Later), 3)}}, InputLoc,
                RecordLayout{{192, 64}, {0}}},
               {"nct_later", {{"value", intType()}}, InputLoc,
                RecordLayout{{32, 32}, {0}}}};
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  M.Records[0].Fields[0].ValueType = pointerType(arrayType(Later, 3));
  invalid(M, "forward/cyclic");
  M.Records.clear();
  for (const auto &T : {
           arrayType({TypeKind::Void, {}}, 1),
           arrayType(arrayType(intType(), 65536), 65536),
           arrayType(intType(), 0)}) {
    M.Functions[0].Result = pointerType(T);
    invalid(M);
  }
  auto From = pointerType(arrayType(intType(), 2));
  auto To = pointerType(arrayType(intType(), 3));
  M.Functions[0].Params.push_back({"nct_pointer", From, InputLoc});
  M.Functions[0].Result = To;
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Cast, To,
                                            {variable("nct_pointer", From)}));
  invalid(M, "pointer conversion");
}

TEST(TranslateIR, ArrayIRRejectsMalformedTypesAndNonaddressableDecay) {
  auto M = module();
  auto Array = arrayType(intType(), 2);
  M.Functions[0].Locals.push_back({"nct_array", Array, InputLoc});
  invalid(M, "Array types require core v2");
  M.Profile = "cpp-core-v2";
  M.Target.Carriers = x64CarrierLayout();
  auto Malformed = Array;
  Malformed.PointeeConst = true;
  M.Functions[0].Locals[0].ValueType = Malformed;
  invalid(M, "Array types require");
  Malformed = Array;
  Malformed.Elements.push_back(intType());
  M.Functions[0].Locals[0].ValueType = Malformed;
  invalid(M, "Array types require");
  M.Functions[0].Locals[0].ValueType = Array;
  Type Record{TypeKind::Record, "nct_box"};
  M.Records.push_back({"nct_box", {{"values", Array}}, InputLoc,
                       RecordLayout{{64, 32}, {0}}});
  auto Values = pointerExpr(ExprKind::Aggregate, Array,
                            {literal("1"), literal("2")});
  auto Whole = pointerExpr(ExprKind::Aggregate, Record, {Values});
  auto Member = pointerExpr(ExprKind::Member, Array, {Whole});
  Member.Name = "values";
  M.Functions[0].Result = pointerType(intType());
  M.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::ArrayDecay, pointerType(intType()), {Member}));
  invalid(M, "Assignment target");
  M.Functions[0].Body.back().Value->Args[0] = variable("nct_array", Array);
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Result = pointerType(boolType());
  M.Functions[0].Body.back().Value->ValueType = pointerType(boolType());
  invalid(M, "Array decay requires");
}

TEST(TranslateIR, CoreV2ParsesPointerTypesAndEmitsNestedConstDeclarators) {
  for (const auto &Pair : std::vector<std::pair<std::string, std::string>>{
           {"cptr:ptr:int", "int *const * sample(void)"},
           {"ptr:cptr:int", "const int * * sample(void)"},
           {"ptr:void", "void * sample(void)"}}) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + Pair.first + "\"");
    replaceOnce(JSON, "\"kind\": \"literal\", \"type\": \"bool\", \"value\": true,",
                "\"kind\": \"null\", \"type\": \"" + Pair.first + "\",");
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(JSON, M, D));
    ASSERT_TRUE(verifyModule(M, context(M), D));
    EXPECT_EQ(M.Exports.front().Result, Pair.first);
    EmittedSource Output;
    ASSERT_TRUE(emitNC(M, context(M), Output, D));
    EXPECT_NE(Output.Text.find(Pair.second), std::string::npos) << Output.Text;
  }
}

TEST(TranslateIR, CoreV2BoundsPointerWireTypesAndRejectsMalformedTrees) {
  std::string Deep;
  for (unsigned I = 0; I != 66; ++I)
    Deep += "ptr:";
  Deep += "int";
  for (const auto &Spelling :
       std::vector<std::string>{"ptr:", "cptr::int", Deep, std::string(4097, 'a')}) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + Spelling + "\"");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D)) << Spelling;
  }
  std::vector<Type> BadTypes = {
      {TypeKind::Pointer, {}},
      {TypeKind::Pointer, {}, {intType(), intType()}},
      {TypeKind::Int, {}, {intType()}},
      {TypeKind::Int, {}, {}, true},
      pointerType({TypeKind::Record, "nct_missing"}),
      pointerType({TypeKind::Double, {}})};
  Type DeepType = intType();
  for (unsigned I = 0; I != 66; ++I)
    DeepType = pointerType(std::move(DeepType));
  BadTypes.push_back(std::move(DeepType));
  for (auto T : BadTypes) {
    auto M = module(true);
    M.Functions[0].Result = std::move(T);
    invalid(M);
  }
}

TEST(TranslateIR, CoreV2VerifiesPointerAddressesAndConstWrites) {
  auto M = module(true);
  auto Pointer = pointerType(intType());
  auto &F = M.Functions[0];
  F.Params.push_back({"nct_p", Pointer, InputLoc});
  auto Place = pointerExpr(ExprKind::Dereference, intType(),
                           {variable("nct_p", Pointer)});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Place;
  Assign.Value = literal("7");
  F.Body = {label(), Assign, ret(Place)};
  Diagnostics D;
  ASSERT_TRUE(verifyModule(M, context(M), D));
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("(*(nct_p)) = (7);"), std::string::npos);

  auto Readonly = pointerType(intType(), true);
  F.Params[0].ValueType = Readonly;
  F.Body[1].Target->Args[0].ValueType = Readonly;
  F.Body.back().Value->Args[0].ValueType = Readonly;
  invalid(M, "Const pointee");

  F.Body = {label(), ret(literal("0"))};
  F.Result = Pointer;
  M.Globals.push_back({"nct_constant", intType(), literal("3"), InputLoc});
  F.Body.back() = ret(pointerExpr(ExprKind::Address, Pointer,
                                 {variable("nct_constant")}));
  invalid(M, "Global constants");
  F.Result = Readonly;
  F.Body.back().Value->ValueType = Readonly;
  D.clear();
  EXPECT_TRUE(verifyModule(M, context(M), D));
  F.Body.back().Value->Args[0] = literal("3");
  invalid(M, "Assignment target");
}

TEST(TranslateIR, CoreV2ConstRecordDoesNotMakeItsPointerPointeeConst) {
  auto M = module(true);
  Type RecordType{TypeKind::Record, "nct_box"};
  Type Pointer = pointerType(intType());
  M.Records.push_back({"nct_box", {{"value", Pointer}}, InputLoc,
                       RecordLayout{{64, 64}, {0}}});
  Type View = pointerType(RecordType, true);
  M.Functions[0].Params.push_back({"nct_box_pointer", View, InputLoc});
  Expr Field = pointerExpr(ExprKind::Member, Pointer,
      {pointerExpr(ExprKind::Dereference, RecordType,
                   {variable("nct_box_pointer", View)})});
  Field.Name = "value";
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = pointerExpr(ExprKind::Dereference, intType(), {Field});
  Assign.Value = literal("9");
  M.Functions[0].Body = {label(), Assign, ret(literal("0"))};
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Body[1].Target = Field;
  M.Functions[0].Body[1].Value = pointerExpr(ExprKind::Null, Pointer);
  invalid(M, "Const pointee");
}

TEST(TranslateIR, CoreV2DistinguishesPointerGraphsFromByValueCycles) {
  auto M = module(true);
  Type A{TypeKind::Record, "nct_a"}, B{TypeKind::Record, "nct_b"};
  M.Records = {{"nct_a", {{"next", pointerType(B)}}, InputLoc,
                RecordLayout{{64, 64}, {0}}},
               {"nct_b", {{"next", pointerType(A)}}, InputLoc,
                RecordLayout{{64, 64}, {0}}}};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_LT(Output.Text.find("typedef struct nct_b nct_b;"),
            Output.Text.find("struct nct_a {"));
  M.Records[0].Fields[0].ValueType = B;
  invalid(M, "forward/cyclic");
  M.Records[0].Fields[0].ValueType = pointerType({TypeKind::Record, "nct_unknown"});
  invalid(M, "Unknown");
}

TEST(TranslateIR, CoreV2PointerOffsetsVerifyPromotionsAndExactResultTypes) {
  for (auto Element : {intType(), integerType(8), arrayType(intType(), 3),
                       pointerType(intType())}) {
    for (bool Const : {false, true}) {
      auto P = pointerType(Element, Const);
      for (auto Offset : {intType(), uintType(), integerType(64), integerType(64, true)}) {
        auto M = module(true);
        M.Functions[0].Result = P;
        M.Functions[0].Params = {{"nct_p", P, InputLoc}, {"nct_i", Offset, InputLoc}};
        auto Pointer = variable("nct_p", P), Index = variable("nct_i", Offset);
        for (auto Op : {BinaryOperator::Add, BinaryOperator::Subtract}) {
          M.Functions[0].Body.back() = ret(binary(Op, Pointer, Index, P));
          Diagnostics D;
          EmittedSource Output;
          ASSERT_TRUE(emitNC(M, context(M), Output, D));
          EXPECT_NE(Output.Text.find("nct_emit_right == 0 ? nct_emit_left"),
                    std::string::npos);
        }
        M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add, Index, Pointer, P));
        Diagnostics D;
        EXPECT_TRUE(verifyModule(M, context(M), D));
        M.Functions[0].Body.back().Value->BinaryOp = BinaryOperator::Subtract;
        invalid(M, "Pointer offset");
      }
    }
  }
  auto M = module(true);
  auto P = pointerType(intType());
  M.Functions[0].Result = P;
  M.Functions[0].Params = {{"nct_p", P, InputLoc}};
  auto Pointer = variable("nct_p", P);
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add, Pointer,
      literal("1", integerType(8)), P));
  invalid(M, "promoted integer");
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add, Pointer, literal("1"),
      pointerType(intType(), true)));
  invalid(M, "matching result");
  auto VoidPointer = pointerType({TypeKind::Void, {}});
  M.Functions[0].Result = VoidPointer;
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Add,
      pointerExpr(ExprKind::Null, VoidPointer), literal("0"), VoidPointer));
  invalid(M, "complete pointee");
}

TEST(TranslateIR, CoreV2PointerDifferenceRequiresIndependentNativePtrdiff) {
  auto M = module(true);
  auto P = pointerType(intType()), CP = pointerType(intType(), true);
  auto Difference = integerType(64);
  M.Functions[0].Result = Difference;
  M.Functions[0].Params = {{"nct_p", P, InputLoc}, {"nct_q", CP, InputLoc}};
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Subtract,
      variable("nct_p", P), variable("nct_q", CP), Difference));
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("nct_emit_left == nct_emit_right ? (long long)0"),
            std::string::npos);
  EXPECT_NE(Output.Text.find("translated ptrdiff width mismatch"), std::string::npos);
  EXPECT_NE(Output.Text.find("translated ptrdiff must be signed"), std::string::npos);
  for (unsigned BadWidth : {0u, 16u, 32u}) {
    auto C = context(M);
    C.ExpectedPtrDiffBits = BadWidth;
    D.clear();
    EXPECT_FALSE(verifyModule(M, C, D));
  }
  for (auto BadResult : {intType(), integerType(64, true)}) {
    auto Bad = M;
    Bad.Functions[0].Result = BadResult;
    Bad.Functions[0].Body.back().Value->ValueType = BadResult;
    invalid(Bad, "ptrdiff width");
  }
  for (auto Other : {pointerType(uintType()), pointerType(integerType(8)),
                    pointerType(arrayType(intType(), 2)),
                    pointerType({TypeKind::Void, {}})}) {
    auto Bad = M;
    Bad.Functions[0].Params[1].ValueType = Other;
    Bad.Functions[0].Body.back().Value->Args[1].ValueType = Other;
    invalid(Bad, "matching complete pointees");
  }
  auto Bad = M;
  auto Nested = pointerType(pointerType(intType()));
  auto DeepConst = pointerType(pointerType(intType(), true));
  Bad.Functions[0].Params[0].ValueType = Nested;
  Bad.Functions[0].Params[1].ValueType = DeepConst;
  Bad.Functions[0].Body.back().Value->Args[0].ValueType = Nested;
  Bad.Functions[0].Body.back().Value->Args[1].ValueType = DeepConst;
  invalid(Bad, "matching complete pointees");
}

TEST(TranslateIR, CoreV2PointerAddressCancellationAndNestedOffsetsStayBounded) {
  auto M = module(true);
  auto P = pointerType(intType());
  M.Functions[0].Result = P;
  M.Functions[0].Params = {{"nct_p", P, InputLoc}};
  auto Pointer = variable("nct_p", P);
  auto Index = pointerExpr(ExprKind::Index, intType(), {Pointer, literal("0")});
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Address, P, {Index}));
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("nct_emit_right == 0 ? nct_emit_left"), std::string::npos);
  EXPECT_EQ(Output.Text.find("(&("), std::string::npos);
  auto CP = pointerType(intType(), true);
  M.Functions[0].Result = CP;
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Address, CP,
      {pointerExpr(ExprKind::Dereference, intType(), {Pointer})}));
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_EQ(Output.Text.find("(*("), std::string::npos);
  EXPECT_NE(Output.Text.find("((const int *)(nct_p))"), std::string::npos);
  M.Functions[0].Result = P;
  auto Nested = Pointer;
  for (unsigned Depth = 0; Depth < 48; ++Depth)
    Nested = binary(BinaryOperator::Add, std::move(Nested), literal("0"), P);
  M.Functions[0].Body.back() = ret(std::move(Nested));
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_LT(Output.Text.size(), 16000u);
  EXPECT_EQ(Output.Text.find("nct_emit_pointer_1"), std::string::npos);
  M.Profile = "cpp-core-v1";
  M.Target.Carriers.reset();
  invalid(M, "core v2");
}

TEST(TranslateIR, CoreV2RejectsPointerIntegerCastsAndOrdering) {
  Type Pointer = pointerType(intType());
  for (const auto &Pair : std::vector<std::pair<Type, Type>>{
           {Pointer, intType()}, {intType(), Pointer},
           {boolType(), Pointer}, {Pointer, pointerType(boolType())}}) {
    auto M = module(true);
    M.Functions[0].Result = Pair.second;
    M.Functions[0].Params.push_back({"nct_value", Pair.first, InputLoc});
    M.Functions[0].Body.back() = ret(pointerExpr(
        ExprKind::Cast, Pair.second, {variable("nct_value", Pair.first)}));
    invalid(M, "pointer conversion");
  }
  auto M = module(true);
  M.Functions[0].Result = boolType();
  auto Null = pointerExpr(ExprKind::Null, Pointer);
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Equal, Null, Null, boolType()));
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  M.Functions[0].Body.back().Value->BinaryOp = BinaryOperator::Less;
  invalid(M, "ordering");
}

TEST(TranslateIR, PointerNodesCannotBypassProfileOrPointeeChecks) {
  Type Pointer = pointerType(intType());
  auto Null = pointerExpr(ExprKind::Null, Pointer);
  for (const auto &Value : {
           Null,
           pointerExpr(ExprKind::Address, Pointer, {literal("1")}),
           pointerExpr(ExprKind::Dereference, intType(), {Null})}) {
    auto M = module();
    M.Functions[0].Result = Value.ValueType;
    M.Functions[0].Body.back() = ret(Value);
    invalid(M, "core v2");
  }
  for (const auto &Value : {
           pointerExpr(ExprKind::Null, intType()),
           pointerExpr(ExprKind::Null, Pointer, {literal("0")}),
           pointerExpr(ExprKind::Dereference, boolType(), {Null}),
           pointerExpr(ExprKind::Dereference, intType(),
                        {pointerExpr(ExprKind::Null, pointerType({TypeKind::Void, {}}))})}) {
    auto M = module(true);
    M.Functions[0].Result = Value.ValueType;
    M.Functions[0].Body.back() = ret(Value);
    invalid(M);
  }
}

TEST(TranslateIR, CoreV2RetainsSingleUnitAndNoMathContract) {
  Module Base = module(true);
  Module M = Base;
  M.Dependencies.push_back({"owned.hpp", std::string(64, 'b')});
  invalid(M, "exactly one source dependency");
  M = Base;
  M.FPContractID = CppMathFPContractID;
  invalid(M, "Math metadata");
  M = Base;
  M.SDKDistributionID = "unapproved-sdk";
  invalid(M, "Math metadata");
  M = Base;
  M.Functions.front().Result = {TypeKind::Double, {}};
  invalid(M, "Double requires");
  M = Base;
  Instruction Call;
  Call.Op = InstructionKind::MappedCall;
  Call.Loc = InputLoc;
  Call.MappingID = "cpp.math.fabs.f64.v1";
  M.Functions.front().Body.insert(M.Functions.front().Body.begin() + 1, Call);
  invalid(M, "Mapped call");
}

TEST(TranslateIR, CoreV2WireRejectsMathEvidenceBeforeEmission) {
  for (const auto *Extra : {
           "\"fp_contract\":\"cpp.math.binary64.masked.v1\",",
           "\"sdk_distribution_id\":\"unapproved-sdk\",",
           "\"sdk_catalog_sha256\":\"unapproved\",",
           "\"sdk_dependencies\":[],"}) {
    SCOPED_TRACE(Extra);
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"records\": []", std::string(Extra) + "\"records\": []");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D));
    ASSERT_FALSE(D.empty());
    EXPECT_EQ(D.front().Code, "TR0103");
  }
}

TEST(TranslateIR, RejectsMalformedAndIncompatibleWireData) {
  for (const auto &Change : std::vector<std::pair<std::string, std::string>>{
           {"\"protocol\": 1", "\"protocol\": 2"},
           {"\"value\": true", "\"value\": \"true\""},
           {"\"kind\": \"literal\"", "\"kind\": \"source_code\""},
           {"\"line\": 2", "\"line\": -1"},
           {"\"internal\": false", "\"internal\": 0"}}) {
    auto JSON = wireModule();
    replaceOnce(JSON, Change.first, Change.second);
    Module M = module();
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D));
    ASSERT_FALSE(D.empty());
    EXPECT_EQ(D.front().Code, "TR0103");
    EXPECT_EQ(M.Functions.front().Result, intType());
  }
}

TEST(TranslateIR, BoundsWireNestingBeforeJSONParsing) {
  Module M;
  Diagnostics D;
  std::string JSON(MaxProtocolDepth + 1, '[');
  JSON += std::string(MaxProtocolDepth + 1, ']');
  EXPECT_FALSE(parseModule(JSON, M, D));
  ASSERT_FALSE(D.empty());
  EXPECT_NE(D.front().Reason.find("depth"), std::string::npos);
}

TEST(TranslateIR, RejectsInvalidIntegerLiterals) {
  for (const auto &Value :
       {"+1", "01", "-0", "2147483648", "-2147483649", "1;return 0"}) {
    Module M = module();
    M.Functions.front().Body.back().Value = literal(Value);
    invalid(M, "literal");
  }
  for (const auto &Value : {"4294967296", "-1"}) {
    Module M = module();
    M.Functions.front().Result = uintType();
    M.Functions.front().Body.back().Value = literal(Value, uintType());
    invalid(M, "literal");
  }
}

TEST(TranslateIR, RejectsDuplicateShadowingAndReservedNames) {
  Module M = module();
  M.Functions.push_back(M.Functions.front());
  invalid(M, "duplicate");
  M = module();
  M.Functions.front().Name = "restrict";
  invalid(M, "identifier");
  M = module();
  M.Functions.front().Locals = {{"nct_emit_u32_to_i32", intType(), InputLoc}};
  invalid(M, "identifier");
  M = module();
  M.Globals = {{"nct_constant", intType(), literal("7"), InputLoc}};
  M.Functions.front().Locals = {{"nct_constant", intType(), InputLoc}};
  invalid(M, "shadowing");
}

TEST(TranslateIR, RejectsUnknownVariablesAndMismatchedOperators) {
  Module M = module();
  M.Functions.front().Body.back().Value = variable("nct_missing");
  invalid(M, "Unknown variable");
  M = module();
  M.Functions.front().Body.back().Value =
      binary(BinaryOperator::Add, literal("1"), literal("2", uintType()));
  invalid(M, "same converted type");
  M = module();
  M.Functions.front().Body.back().Value =
      binary(BinaryOperator::Less, literal("1"), literal("2"));
  invalid(M, "result type");
}

TEST(TranslateIR, RejectsBrokenControlFlowAndCallSignatures) {
  Module M = module();
  M.Functions.front().Body.pop_back();
  invalid(M, "terminator");
  M = module();
  M.Functions.front().Body.push_back(ret(literal("1")));
  invalid(M, "after a block terminator");
  M = module();
  Instruction Jump;
  Jump.Op = InstructionKind::Jump;
  Jump.Loc = InputLoc;
  Jump.Label = "nct_missing";
  M.Functions.front().Body.back() = Jump;
  invalid(M, "unknown label");
  M = module();
  Instruction Call;
  Call.Op = InstructionKind::Call;
  Call.Loc = InputLoc;
  Call.Callee = "missing";
  M.Functions.front().Body.insert(M.Functions.front().Body.begin() + 1, Call);
  invalid(M, "selected definition");
  M = module();
  Call.Callee = "sample";
  M.Functions.front().Body.insert(M.Functions.front().Body.begin() + 1, Call);
  invalid(M, "result storage");
}

TEST(TranslateIR, VerifiesRecordDependencyAndConstantRules) {
  Type RecordT{TypeKind::Record, "nct_record"};
  Module M = module();
  M.Records = {{"nct_record", {{"field", RecordT}}, InputLoc}};
  invalid(M, "cyclic");
  M = module();
  M.Globals = {{"nct_constant", intType(),
                binary(BinaryOperator::Add, literal("1"), literal("2")),
                InputLoc}};
  invalid(M, "folded");
  M = module();
  M.Records = {{"nct_record", {{"field", intType()}}, InputLoc}};
  Expr Aggregate;
  Aggregate.Kind = ExprKind::Aggregate;
  Aggregate.ValueType = RecordT;
  Aggregate.Loc = InputLoc;
  Aggregate.Args = {literal("7")};
  M.Globals = {{"nct_constant", RecordT, Aggregate, InputLoc}};
  Expr Member;
  Member.Kind = ExprKind::Member;
  Member.Loc = InputLoc;
  Member.ValueType = intType();
  Member.Name = "field";
  Member.Args = {variable("nct_constant", RecordT)};
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Member;
  Assign.Value = literal("9");
  M.Functions.front().Body.insert(M.Functions.front().Body.begin() + 1, Assign);
  invalid(M, "not writable");
  M.Functions.front().Body[1].Target->Args = {Aggregate};
  invalid(M, "mutable local");
}

TEST(TranslateIR, RejectsTargetAndDependencyMismatches) {
  Module M = module();
  auto C = context(M);
  C.TargetTriple = "aarch64-unknown-linux-gnu";
  Diagnostics D;
  EXPECT_FALSE(verifyModule(M, C, D));
  ASSERT_FALSE(D.empty());
  EXPECT_EQ(D.front().Code, "TR0204");
  for (const auto &Target :
       {"aarch64-apple-ios", "aarch64-unknown-linux-android"}) {
    M = module();
    M.Target.Triple = Target;
    D.clear();
    EXPECT_FALSE(verifyModule(M, context(M), D));
    ASSERT_FALSE(D.empty());
    EXPECT_EQ(D.front().Code, "TR0204");
  }
  for (const auto &Path :
       {"../input.cpp", "/input.cpp", "input.cpp/", "dir//input.cpp"}) {
    M = module();
    M.Dependencies.front().Path = Path;
    invalid(M, "dependency");
  }
}

TEST(TranslateIR, EmitsDeterministicMappedSourceWithoutSyntheticMain) {
  Module M = module();
  EmittedSource A, B;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), A, D));
  ASSERT_TRUE(emitNC(M, context(M), B, D));
  EXPECT_EQ(A.Text, B.Text);
  EXPECT_EQ(A.Text.find("main("), std::string::npos);
  EXPECT_NE(A.Text.find("int sample(void)"), std::string::npos);
  ASSERT_FALSE(A.Map.empty());
  const auto Lines = std::count(A.Text.begin(), A.Text.end(), '\n');
  for (const auto &E : A.Map) {
    EXPECT_GE(E.BeginLine, 1u);
    EXPECT_GE(E.EndLine, E.BeginLine);
    EXPECT_LE(E.EndLine, static_cast<uint32_t>(Lines));
    EXPECT_EQ(E.Original.File, "input.cpp");
  }
}

class TranslateIREmissionTest : public NeverCTest {};

TEST_F(TranslateIREmissionTest, GeneratedGuardRejectsDifferentArchitecture) {
  Module M = module();
  M.Target.Triple = hostTriple();
  M.Target.PointerBits = sizeof(void *) * 8;
  EmittedSource Out;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  const auto Source = tmpFile("target_guard.nc");
  writeFile(Source, Out.Text);
  std::string Opposite = isArm64() ? "x86_64" : "aarch64";
  Opposite += isDarwin()    ? "-apple-macosx"
              : isWindows() ? "-pc-windows-msvc"
                            : "-unknown-linux-gnu";
  std::vector<std::string> Args{Source.string(), "-fsyntax-only", "-target",
                                Opposite};
  auto Sysroot = sysrootFlags();
  Args.insert(Args.end(), Sysroot.begin(), Sysroot.end());
  const auto Compile = ncc(Args);
  EXPECT_FALSE(Compile.ok());
  EXPECT_TRUE(
      Compile.stderrContains("requires its recorded target architecture"))
      << Compile.err;
}

TEST_F(TranslateIREmissionTest, SignedShiftAndConversionValuesAtO0AndO2) {
  Module M = module();
  M.Target.Triple = hostTriple();
  M.Target.PointerBits = sizeof(void *) * 8;
  M.Functions.clear();
  auto AddFunction = [&](std::string Name, std::vector<Variable> Params,
                         Expr Value) {
    Function F;
    F.Name = std::move(Name);
    F.Result = intType();
    F.CExport = true;
    F.Loc = InputLoc;
    F.Params = std::move(Params);
    F.Body = {label(), ret(std::move(Value))};
    M.Functions.push_back(std::move(F));
  };
  const std::vector<Variable> ShiftParams{{"nct_a", intType(), InputLoc},
                                          {"nct_b", intType(), InputLoc}};
  AddFunction(
      "test_shl", ShiftParams,
      binary(BinaryOperator::ShiftLeft, variable("nct_a"), variable("nct_b")));
  AddFunction(
      "test_shr", ShiftParams,
      binary(BinaryOperator::ShiftRight, variable("nct_a"), variable("nct_b")));
  Expr Cast;
  Cast.Kind = ExprKind::Cast;
  Cast.ValueType = intType();
  Cast.Loc = InputLoc;
  Cast.Args = {variable("nct_u", uintType())};
  AddFunction("test_cast", {{"nct_u", uintType(), InputLoc}}, Cast);
  EmittedSource Out;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  EXPECT_NE(Out.Text.find("4294967295u - nct_emit_u"), std::string::npos);
  Out.Text += R"nc(
int main(void) {
  if (test_shl(1, 31) != (-2147483647 - 1)) return 1;
  if (test_shl(3, 30) != -1073741824) return 2;
  if (test_shl(1073741824, 1) != (-2147483647 - 1)) return 3;
  if (test_shr(-3, 1) != -2) return 4;
  if (test_shr((-2147483647 - 1), 31) != -1) return 5;
  if (test_shr(2147483647, 30) != 1) return 6;
  if (test_cast(4294967295u) != -1) return 7;
  if (test_cast(2147483648u) != (-2147483647 - 1)) return 8;
  if (test_cast(2147483647u) != 2147483647) return 9;
  return 0;
}
)nc";
  const auto Source = tmpFile("integer_semantics.nc");
  writeFile(Source, Out.Text);
  for (const auto &Opt : {"-O0", "-O2"}) {
    auto Exe = tmpFile(std::string("integer_semantics") + Opt +
                       (isWindows() ? ".exe" : ""));
    std::vector<std::string> Args{Source.string(), Opt, "-o", Exe.string()};
    for (const auto &Flags : {archFlags(), sysrootFlags(), linkFlags()})
      Args.insert(Args.end(), Flags.begin(), Flags.end());
    const auto Compile = ncc(Args);
    ASSERT_TRUE(Compile.ok()) << Compile.err;
    const auto Run = exec(Exe.string(), {});
    EXPECT_TRUE(Run.ok()) << "Full-width integer comparison failed at " << Opt
                          << ": " << Run.exitCode << Run.err;
  }
}
