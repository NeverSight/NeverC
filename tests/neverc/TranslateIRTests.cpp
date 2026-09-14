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
                 {32, 32}, {32, 32}, {64, 64}, {64, 64}, {64, 64},
                 {32, 32}, {64, 64}}};
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
    C.ExpectedUIntPtrBits = 64;
    C.HasLockFreeIntAtomics = true;
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
      pointerType({TypeKind::Double, "nct_invalid"})};
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

TEST(TranslateIR, CoreV2MutableScalarGlobalsPreserveWritesAndAddressIdentity) {
  for (const auto &T : {intType(), uintType(), boolType(), integerType(8), integerType(64, true)}) {
    auto M = module(true);
    auto Initial = literal("0", T);
    auto Value = literal("1", T);
    if (T.Kind == TypeKind::Bool) {
      Initial.Integer.clear();
      Value.Integer.clear();
      Value.Boolean = true;
    }
    M.Globals.push_back({"nct_mutable", T, Initial, InputLoc, true});
    M.Globals.push_back({"nct_readonly", intType(), literal("7"), InputLoc});
    auto &F = M.Functions[0];
    F.Result = T;
    Instruction Assign;
    Assign.Op = InstructionKind::Assign;
    Assign.Loc = InputLoc;
    Assign.Target = variable("nct_mutable", T);
    Assign.Value = Value;
    F.Body = {label(), Assign, ret(variable("nct_mutable", T))};
    Diagnostics D;
    ASSERT_TRUE(verifyModule(M, context(M), D));
    EmittedSource Output;
    ASSERT_TRUE(emitNC(M, context(M), Output, D));
    auto At = Output.Text.find(" nct_mutable = ");
    ASSERT_NE(At, std::string::npos);
    auto Start = Output.Text.rfind('\n', At);
    auto Declaration = Output.Text.substr(Start == std::string::npos ? 0 : Start+1,
                                          At - (Start == std::string::npos ? 0 : Start+1));
    EXPECT_EQ(Declaration.find("static "), 0u);
    EXPECT_NE(Declaration.find("static const "), 0u);
    EXPECT_NE(Output.Text.find("static const int nct_readonly = (7);"), std::string::npos);
    auto P = pointerType(T);
    F.Result = P;
    F.Body.back() = ret(pointerExpr(ExprKind::Address, P, {variable("nct_mutable", T)}));
    D.clear();
    ASSERT_TRUE(verifyModule(M, context(M), D));
    F.Body = {label(), F.Body.back()};
    M.Globals[0].Mutable = false;
    invalid(M, "Global constants");
  }
}

TEST(TranslateIR, MutableGlobalsRetainProfileTypeAndInitializerBoundaries) {
  auto Old = module();
  Old.Globals.push_back({"nct_mutable", intType(), literal("0"), InputLoc, true});
  invalid(Old, "Mutable globals require core v2");
  for (const auto &T : {Type{TypeKind::Void, {}}}) {
    auto M = module(true);
    M.Globals.push_back({"nct_mutable", T, literal("0", T), InputLoc, true});
    invalid(M, "Mutable globals require core v2 numeric, boolean, nullptr, pointer, callback, record or fixed-array storage");
  }
  auto M = module(true);
  M.Globals.push_back({"nct_mutable", integerType(8, true), literal("256", integerType(8, true)), InputLoc, true});
  invalid(M, "literal");
  M.Globals[0] = {"nct_mutable", intType(), binary(BinaryOperator::Add, literal("1"), literal("2")), InputLoc, true};
  invalid(M, "folded");
  M.Globals[0].Value = literal("0");
  M.Globals.push_back(M.Globals[0]);
  invalid(M, "duplicate");
}

TEST(TranslateIR, GlobalMutabilityWireDefaultsToConstAndRequiresCoreV2Boolean) {
  const std::string Global = R"json({"name":"nct_global","type":"int",
    "value":{"kind":"literal","type":"int","value":"0",
      "loc":{"file":"input.cpp","line":1,"column":1}},
    "loc":{"file":"input.cpp","line":1,"column":1}})json";
  for (const std::string &Marker : {"", ",\"mutable\":false", ",\"mutable\":true"}) {
    auto G = Global;
    G.insert(G.size()-1, Marker);
    auto Wire = wireModule(true);
    replaceOnce(Wire, "\"globals\": []", "\"globals\": [" + G + "]");
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(Wire, M, D));
    ASSERT_EQ(M.Globals.size(), 1u);
    EXPECT_EQ(M.Globals[0].Mutable, Marker == ",\"mutable\":true");
    EXPECT_TRUE(verifyModule(M, context(M), D));
  }
  for (const std::string &Value : {"null", "0", "1", "\"true\"", "[]", "{}"}) {
    auto G = Global;
    G.insert(G.size()-1, ",\"mutable\":" + Value);
    auto Wire = wireModule(true);
    replaceOnce(Wire, "\"globals\": []", "\"globals\": [" + G + "]");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(Wire, M, D));
  }
  for (const std::string &Profile : {"cpp-core-v1", "cpp-project-v1", "cpp-math-v1"}) {
    auto G = Global;
    G.insert(G.size()-1, ",\"mutable\":false");
    auto Wire = wireModule();
    replaceOnce(Wire, "cpp-core-v1", Profile);
    if (Profile == "cpp-math-v1")
      Wire.insert(1, "\"fp_contract\":\"fixture\",\"sdk_distribution_id\":\"fixture\","
                     "\"sdk_catalog_sha256\":\"fixture\",\"sdk_dependencies\":[],");
    replaceOnce(Wire, "\"globals\": []", "\"globals\": [" + G + "]");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(Wire, M, D));
    ASSERT_FALSE(D.empty());
    EXPECT_TRUE(std::any_of(D.begin(), D.end(), [](const Diagnostic &E) {
      return E.Reason.find("Global mutability evidence requires core v2") != std::string::npos;
    }));
  }
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

TEST(TranslateIR, CoreV2RejectsPointerIntegerCasts) {
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
}

TEST(TranslateIR, CoreV2PointerOrderingRequiresIndependentAddressRepresentation) {
  for (auto P : {pointerType(intType()), pointerType(intType(), true),
                 pointerType(arrayType(intType(), 3)),
                 pointerType(pointerType(intType(), true), true)}) {
    for (auto Op : {BinaryOperator::Less, BinaryOperator::LessEqual,
                    BinaryOperator::Greater, BinaryOperator::GreaterEqual}) {
      auto M = module(true);
      M.Functions[0].Result = boolType();
      M.Functions[0].Params = {{"nct_p", P, InputLoc}, {"nct_q", P, InputLoc}};
      M.Functions[0].Body.back() = ret(binary(Op, variable("nct_p", P),
                                            variable("nct_q", P), boolType()));
      Diagnostics D;
      EmittedSource Output;
      ASSERT_TRUE(emitNC(M, context(M), Output, D));
      EXPECT_NE(Output.Text.find("translated pointer ordering width mismatch"),
                std::string::npos);
      EXPECT_NE(Output.Text.find("(__UINTPTR_TYPE__)(nct_p)"), std::string::npos);
      EXPECT_EQ(Output.Text.find("translated ptrdiff width mismatch"), std::string::npos);
      for (unsigned BadWidth : {0u, 16u, 32u}) {
        auto C = context(M);
        C.ExpectedUIntPtrBits = BadWidth;
        D.clear();
        EXPECT_FALSE(verifyModule(M, C, D));
      }
      auto Bad = M;
      Bad.Functions[0].Result = intType();
      Bad.Functions[0].Body.back().Value->ValueType = intType();
      invalid(Bad, "ordering");
      Bad = M;
      Bad.Functions[0].Params[1].ValueType = uintType();
      Bad.Functions[0].Body.back().Value->Args[1].ValueType = uintType();
      invalid(Bad, "ordering");
      Bad = M;
      const auto Other = pointerType(boolType());
      Bad.Functions[0].Params[1].ValueType = Other;
      Bad.Functions[0].Body.back().Value->Args[1].ValueType = Other;
      invalid(Bad, "ordering");
    }
  }
  auto M = module(true);
  M.Functions[0].Result = boolType();
  const auto VoidPointer = pointerType({TypeKind::Void, {}});
  auto Null = pointerExpr(ExprKind::Null, VoidPointer);
  M.Functions[0].Body.back() = ret(binary(BinaryOperator::Less, Null, Null, boolType()));
  invalid(M, "ordering");
  M.Functions[0].Body.back().Value->BinaryOp = BinaryOperator::Equal;
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
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

namespace {
Module dynamicStaticModule() {
  auto M = module(true);
  M.Globals.push_back({"nct_static", intType(), literal("0"), InputLoc, false, true});
  auto &F = M.Functions[0];
  F.Params.push_back({"nct_argument", intType(), InputLoc});
  Instruction Begin, Store, End, Jump;
  Begin.Op = InstructionKind::StaticInitBegin;
  Begin.GlobalName = "nct_static";
  Begin.TrueLabel = "nct_initialize";
  Begin.FalseLabel = "nct_ready";
  Store.Op = InstructionKind::Assign;
  Store.Target = variable("nct_static");
  Store.Value = variable("nct_argument");
  End.Op = InstructionKind::StaticInitEnd;
  End.GlobalName = "nct_static";
  Jump.Op = InstructionKind::Jump;
  Jump.Label = "nct_ready";
  Begin.Loc = Store.Loc = End.Loc = Jump.Loc = InputLoc;
  F.Body = {label(), Begin, label("nct_initialize"), Store, End, Jump,
            label("nct_ready"), ret(variable("nct_static"))};
  return M;
}
} // namespace

TEST(TranslateIR, CoreV2DynamicStaticStorageRequiresIndependentAtomicEvidence) {
  auto M = dynamicStaticModule();
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static int nct_static = (0);"), std::string::npos);
  EXPECT_EQ(Output.Text.find("static const int nct_static"), std::string::npos);
  EXPECT_NE(Output.Text.find("__NEVERC_ATOMIC_INT_LOCK_FREE == 2"), std::string::npos);
  EXPECT_NE(Output.Text.find("target(\"no-outline-atomics\"), noinline"), std::string::npos);
  EXPECT_NE(Output.Text.find("1u, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE"), std::string::npos);
  EXPECT_NE(Output.Text.find(", 2u, __ATOMIC_RELEASE)"), std::string::npos);
  auto C = context(M);
  C.HasLockFreeIntAtomics = false;
  EXPECT_FALSE(verifyModule(M, C, D));
  Output.Text = "unchanged";
  EXPECT_FALSE(emitNC(M, C, Output, D));
  EXPECT_EQ(Output.Text, "unchanged");
  auto Bad = M;
  Bad.Globals[0].Value = literal("1");
  invalid(Bad, "initializer");
  Bad = M;
  Bad.Profile = "cpp-core-v1";
  Bad.Target.Carriers.reset();
  invalid(Bad, "core v2");
  // A declaration eliminated as dead code can still retain its zero storage.
  M.Functions[0].Body = {label(), ret(literal("0"))};
  EXPECT_TRUE(verifyModule(M, context(M), D));
  for (bool Single : {false, true}) {
    auto T = Type{Single ? TypeKind::Float : TypeKind::Double, {}};
    M.Globals[0].ValueType = T;
    M.Globals[0].Value = literal({}, T);
    EXPECT_TRUE(verifyModule(M, context(M), D));
    M.Globals[0].Value.Binary64Bits = Single ? UINT64_C(0x80000000)
                                           : UINT64_C(0x8000000000000000);
    invalid(M, "initializer");
  }
}

TEST(TranslateIR, CoreV2DynamicStaticOwnershipRejectsForgedControlFlowAndWrites) {
  auto M = dynamicStaticModule();
  auto Bad = M;
  Bad.Functions[0].Body[1].FalseLabel = "nct_initialize";
  invalid(Bad, "ownership");
  Bad = M;
  Bad.Functions[0].Body[1].TrueLabel = "nct_ready";
  invalid(Bad, "ownership");
  Bad = M;
  Bad.Functions[0].Body[1].GlobalName = "nct_missing";
  invalid(Bad, "dynamic global");
  Bad = M;
  Bad.Functions[0].Body.erase(Bad.Functions[0].Body.begin() + 4);
  invalid(Bad, "ownership");
  Bad = M;
  Bad.Functions[0].Body[4] = ret(literal("0"));
  invalid(Bad, "unfinished");
  Bad = M;
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + 7,
                               Bad.Functions[0].Body[3]);
  invalid(Bad, "not writable");
  Bad = M;
  Bad.Functions[0].Result = pointerType(intType());
  Bad.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::Address, pointerType(intType()), {variable("nct_static")}));
  invalid(Bad, "not writable");
  Bad.Functions[0].Result.PointeeConst = true;
  Bad.Functions[0].Body.back().Value->ValueType.PointeeConst = true;
  Diagnostics D;
  EXPECT_TRUE(verifyModule(Bad, context(Bad), D));
  Bad = M;
  Bad.Functions.push_back(Bad.Functions[0]);
  Bad.Functions.back().Name = "second";
  invalid(Bad, "more than one initialization site");
  Bad = M;
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + 5,
                               Bad.Functions[0].Body[4]);
  invalid(Bad, "more than one publication site");
  Bad = M;
  Bad.Functions[0].Body.back().GlobalName = "nct_static";
  invalid(Bad, "guard identity");
  Bad = M;
  Bad.Functions[0].Body[1].Value = literal("1");
  invalid(Bad, "exact operands");
  Bad = M;
  Bad.Functions[0].Body[4].TrueLabel = "nct_ready";
  invalid(Bad, "exact operands");
  Bad = M;
  Bad.Globals.push_back({"nct_other", intType(), literal("0"), InputLoc, false, true});
  Bad.Functions[0].Body[4].GlobalName = "nct_other";
  invalid(Bad, "declaring function");
  Bad = M;
  Bad.Globals.push_back({"nct_other", intType(), literal("0"), InputLoc, false, true});
  auto Nested = Bad.Functions[0].Body[1];
  Nested.GlobalName = "nct_other";
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + 3, Nested);
  invalid(Bad, "cannot nest");
  // Permanent nontermination retains ownership; it never falsely publishes.
  auto Loop = M.Functions[0].Body[5];
  Loop.Label = "nct_initialize";
  M.Functions[0].Body.erase(M.Functions[0].Body.begin() + 4);
  M.Functions[0].Body[4] = Loop;
  EXPECT_TRUE(verifyModule(M, context(M), D));
}

TEST(TranslateIR, CoreV2DynamicStaticReferenceCarriersKeepBindingReadonly) {
  auto M = dynamicStaticModule();
  auto Pointer = pointerType(intType());
  M.Globals[0].ValueType = Pointer;
  M.Globals[0].Value = pointerExpr(ExprKind::Null, Pointer);
  auto &F = M.Functions[0];
  F.Result = Pointer;
  F.Params[0].ValueType = Pointer;
  F.Body[3].Target->ValueType = Pointer;
  F.Body[3].Value->ValueType = Pointer;
  F.Body.back().Value->ValueType = Pointer;
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_EQ(Output.Text.find("*const nct_static"), std::string::npos);
  auto Bad = M;
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + 7,
                               Bad.Functions[0].Body[3]);
  invalid(Bad, "not writable");
  Instruction ThroughReference;
  ThroughReference.Op = InstructionKind::Assign;
  ThroughReference.Loc = InputLoc;
  ThroughReference.Target = pointerExpr(
      ExprKind::Dereference, intType(), {variable("nct_static", Pointer)});
  ThroughReference.Value = literal("7");
  F.Body.insert(F.Body.begin() + 7, ThroughReference);
  EXPECT_TRUE(verifyModule(M, context(M), D));
}

namespace {
Module dynamicTemporaryModule() {
  auto M = dynamicStaticModule();
  const auto Pointer = pointerType(intType(), true);
  M.Globals[0].ValueType = Pointer;
  M.Globals[0].Value = pointerExpr(ExprKind::Null, Pointer);
  M.Globals.push_back({"nct_child", intType(), literal("0"), InputLoc,
                       false, false, "nct_static"});
  auto &F = M.Functions[0];
  F.Result = Pointer;
  F.Body[3].Target = variable("nct_static", Pointer);
  F.Body[3].Value = pointerExpr(ExprKind::Address, Pointer, {variable("nct_child")});
  F.Body.back() = ret(variable("nct_static", Pointer));
  Instruction Construct;
  Construct.Op = InstructionKind::Assign;
  Construct.Loc = InputLoc;
  Construct.Target = variable("nct_child");
  Construct.Value = variable("nct_argument");
  F.Body.insert(F.Body.begin() + 3, Construct);
  return M;
}
} // namespace

TEST(TranslateIR, CoreV2DynamicTemporaryGroupsRestrictInitializationAuthority) {
  auto M = dynamicTemporaryModule();
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static int nct_child = (0);"), std::string::npos);
  EXPECT_EQ(Output.Text.find("static const int nct_child"), std::string::npos);
  const auto Guard = Output.Text.find("static _Atomic(unsigned int)");
  ASSERT_NE(Guard, std::string::npos);
  EXPECT_EQ(Output.Text.find("static _Atomic(unsigned int)", Guard + 1), std::string::npos);
  // Readonly addresses can name the child even before its source owner runs.
  // This also forces the emitter's tentative declaration path.
  const auto Pointer = pointerType(intType(), true);
  M.Globals.push_back({"nct_address", Pointer,
                       pointerExpr(ExprKind::Address, Pointer, {variable("nct_child")}), InputLoc});
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static int nct_child;"), std::string::npos);
  EXPECT_EQ(Output.Text.find("static const int nct_child"), std::string::npos);
  for (unsigned At : {1u, 8u}) {
    auto Bad = M;
    auto Store = Bad.Functions[0].Body[3];
    Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + At, Store);
    invalid(Bad, "not writable");
  }
  auto Bad = M;
  Bad.Globals[1].InitializationOwner.clear();
  invalid(Bad, "not writable");
  Bad = M;
  auto Other = Bad.Globals[0];
  Other.Name = "nct_other";
  Bad.Globals.push_back(Other);
  Bad.Globals[1].InitializationOwner = "nct_other";
  invalid(Bad, "not writable");
  Bad = M;
  Bad.Functions[0].Body[1].GlobalName = "nct_child";
  invalid(Bad, "dynamic global");
  Bad = M;
  Bad.Functions[0].Body[5].GlobalName = "nct_child";
  invalid(Bad, "dynamic global");
  // A const pointer alias never acquires the direct-root grant.
  Bad = M;
  Bad.Functions[0].Body[3].Target = pointerExpr(
      ExprKind::Dereference, intType(), {variable("nct_address", Pointer)});
  invalid(Bad, "not writable");
  // Mutable addresses can be formed for construction only in the owned region.
  Bad = M;
  Bad.Functions[0].Result = pointerType(intType());
  Bad.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::Address, pointerType(intType()), {variable("nct_child")}));
  invalid(Bad, "not writable");
  // Unselected or pruned branches need not initialize every child.
  M.Functions[0].Body = {label(), ret(pointerExpr(ExprKind::Null, Pointer))};
  EXPECT_TRUE(verifyModule(M, context(M), D));
}

TEST(TranslateIR, CoreV2DynamicTemporaryGroupsValidateFlatOwnersAndZeroStorage) {
  auto M = dynamicTemporaryModule();
  for (const std::string &Owner : {"nct_missing", "nct_child"}) {
    auto Bad = M;
    Bad.Globals[1].InitializationOwner = Owner;
    invalid(Bad, "object-pointer owner");
  }
  auto Bad = M;
  Bad.Globals[0].Mutable = true;
  invalid(Bad, "object-pointer owner");
  Bad = M;
  Bad.Globals[1].DynamicInitialization = true;
  invalid(Bad, "object-pointer owner");
  Bad = M;
  Bad.Globals[0].InitializationOwner = "nct_child";
  invalid(Bad, "object-pointer owner");
  Bad = M;
  Bad.Globals[0].ValueType = intType();
  Bad.Globals[0].Value = literal("0");
  invalid(Bad, "object-pointer owner");
  Bad = M;
  Bad.Globals[0].ValueType = pointerType({TypeKind::Void, {}});
  Bad.Globals[0].Value = pointerExpr(ExprKind::Null, Bad.Globals[0].ValueType);
  invalid(Bad, "object-pointer owner");
  Bad = M;
  Bad.Globals[1].Value = literal("1");
  invalid(Bad, "initializer");
  Bad = M;
  Bad.Profile = "cpp-core-v1";
  Bad.Target.Carriers.reset();
  invalid(Bad, "core v2");
  M.Functions[0].Body = {label(), ret(pointerExpr(ExprKind::Null, M.Functions[0].Result))};
  const Type Record{TypeKind::Record, "nct_record"};
  M.Records.push_back({"nct_record", {{"nct_first", intType()}, {"nct_second", intType()}},
                       InputLoc, RecordLayout{{64, 32}, {0, 32}}});
  Diagnostics D;
  for (bool Single : {false, true}) {
    auto T = Type{Single ? TypeKind::Float : TypeKind::Double, {}};
    M.Globals[1].ValueType = T;
    M.Globals[1].Value = literal({}, T);
    EXPECT_TRUE(verifyModule(M, context(M), D));
    Bad = M;
    Bad.Globals[1].Value.Binary64Bits = Single ? UINT64_C(0x80000000)
                                            : UINT64_C(0x8000000000000000);
    invalid(Bad, "initializer");
  }
  for (const auto &T : {arrayType(intType(), 2), Record}) {
    M.Globals[1].ValueType = T;
    M.Globals[1].Value = pointerExpr(ExprKind::Aggregate, T, {literal("0"), literal("0")});
    EXPECT_TRUE(verifyModule(M, context(M), D));
    Bad = M;
    Bad.Globals[1].Value.Args[1] = literal("1");
    invalid(Bad, "initializer");
    // A child can precede its owner, and its complete type can differ from
    // the bound subobject type. Association resolution is independent of order.
    std::swap(M.Globals[0], M.Globals[1]);
    EXPECT_TRUE(verifyModule(M, context(M), D));
    std::swap(M.Globals[0], M.Globals[1]);
  }
}

TEST(TranslateIR, CoreV2DynamicTemporaryArrayConstructionKeepsConstDecayChecks) {
  auto M = dynamicTemporaryModule();
  const auto Array = arrayType(intType(), 2);
  M.Globals[1].ValueType = Array;
  M.Globals[1].Value = pointerExpr(ExprKind::Aggregate, Array, {literal("0"), literal("0")});
  const auto MutableDecay = pointerExpr(ExprKind::ArrayDecay, pointerType(intType()),
                                         {variable("nct_child", Array)});
  auto &F = M.Functions[0];
  F.Body[3].Target = pointerExpr(ExprKind::Index, intType(), {MutableDecay, literal("1")});
  F.Body[4].Value = pointerExpr(ExprKind::ArrayDecay, pointerType(intType(), true),
                                {variable("nct_child", Array)});
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  auto Bad = M;
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.begin() + 8, F.Body[3]);
  invalid(Bad, "not writable");
  Bad = M;
  Bad.Functions[0].Body[3].Target->Args[0].ValueType.PointeeConst = true;
  invalid(Bad, "not writable");
}

TEST(TranslateIR, CoreV2DynamicTemporaryRecordConstructionKeepsMemberAndAddressChecks) {
  auto M = dynamicTemporaryModule();
  const Type Record{TypeKind::Record, "nct_record"};
  M.Records.push_back({"nct_record", {{"nct_value", intType()}}, InputLoc,
                       RecordLayout{{32, 32}, {0}}});
  M.Globals[1].ValueType = Record;
  M.Globals[1].Value = pointerExpr(ExprKind::Aggregate, Record, {literal("0")});
  auto Member = pointerExpr(ExprKind::Member, intType(), {variable("nct_child", Record)});
  Member.Name = "nct_value";
  auto &F = M.Functions[0];
  F.Body[3].Target = Member;
  F.Body[4].Value = pointerExpr(ExprKind::Address, pointerType(intType(), true), {Member});
  F.Locals.push_back({"nct_constructor", pointerType(Record), InputLoc});
  Instruction Address;
  Address.Op = InstructionKind::Assign;
  Address.Loc = InputLoc;
  Address.Target = variable("nct_constructor", pointerType(Record));
  Address.Value = pointerExpr(ExprKind::Address, pointerType(Record),
                              {variable("nct_child", Record)});
  F.Body.insert(F.Body.begin() + 3, Address);
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  auto Bad = M;
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.end() - 1, Address);
  invalid(Bad, "not writable");
  Bad = M;
  auto Store = F.Body[4];
  Bad.Functions[0].Body.insert(Bad.Functions[0].Body.end() - 1, Store);
  invalid(Bad, "not writable");
}

TEST(TranslateIR, CoreV2DynamicTemporaryProtocolRequiresExplicitOwnerIdentity) {
  const auto Location = R"json({"file":"input.cpp","line":1,"column":1})json";
  const auto Child = std::string(R"json({"name":"nct_child","type":"int","initialization_owner":"nct_static","loc":)json") +
      Location + R"json(,"value":{"kind":"literal","type":"int","value":"0","loc":)json" + Location + "}}";
  auto Wire = wireModule(true);
  replaceOnce(Wire, "\"globals\": []", "\"globals\": [" + Child + "]");
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(Wire, M, D));
  EXPECT_EQ(M.Globals[0].InitializationOwner, "nct_static");
  EXPECT_FALSE(verifyModule(M, context(M), D));
  for (const std::string &Value : {"false", "null", "17", "[]", "{}", "\"\""}) {
    auto Bad = Wire;
    replaceOnce(Bad, "\"initialization_owner\":\"nct_static\"", "\"initialization_owner\":" + Value);
    EXPECT_FALSE(parseModule(Bad, M, D));
  }
  auto Old = wireModule();
  replaceOnce(Old, "\"globals\": []", "\"globals\": [" + Child + "]");
  EXPECT_FALSE(parseModule(Old, M, D));
}

TEST(TranslateIR, CoreV2DynamicStaticProtocolRejectsMissingOrForeignPayloads) {
  const auto Location = R"json({"file":"input.cpp","line":1,"column":1})json";
  const auto Global = std::string(R"json({"name":"nct_static","type":"int","dynamic_initialization":true,"loc":)json") +
      Location + R"json(,"value":{"kind":"literal","type":"int","value":"0","loc":)json" + Location + "}}";
  for (const auto &Op : {
       R"json({"op":"static_init_begin","global":"nct_static","true":"nct_entry","false":"nct_entry",)json",
       R"json({"op":"static_init_end","global":"nct_static",)json"}) {
    auto Wire = wireModule(true);
    replaceOnce(Wire, "\"globals\": []", "\"globals\": [" + Global + "]");
    auto At = Wire.find("\"body\": [") + std::string("\"body\": [").size();
    const auto Instruction = std::string(Op) + "\"loc\":" + Location + "},";
    Wire.insert(At, Instruction);
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(Wire, M, D));
    EXPECT_TRUE(M.Globals[0].DynamicInitialization);
    EXPECT_EQ(M.Functions[0].Body[0].GlobalName, "nct_static");
    for (const auto &Extra : {"\"value\":{},", "\"target\":{},", "\"args\":[],",
                              "\"callee\":\"nct_other\","}) {
      auto Bad = Wire;
      Bad.insert(At + 1, Extra);
      EXPECT_FALSE(parseModule(Bad, M, D));
    }
    auto Bad = Wire;
    replaceOnce(Bad, "\"global\":\"nct_static\",", "");
    EXPECT_FALSE(parseModule(Bad, M, D));
    Bad = Wire;
    replaceOnce(Bad, "\"dynamic_initialization\":true", "\"dynamic_initialization\":false");
    EXPECT_FALSE(parseModule(Bad, M, D));
  }
}

TEST(TranslateIR, CoreV2MutableStaticRecordsPreserveMemberAndWholeObjectWrites) {
  auto M = module(true);
  const Type Record{TypeKind::Record, "nct_record"};
  M.Records.push_back({"nct_record", {{"nct_value", intType()}}, InputLoc,
                       RecordLayout{{32, 32}, {0}}});
  auto Value = pointerExpr(ExprKind::Aggregate, Record, {literal("3")});
  M.Globals.push_back({"nct_object", Record, Value, InputLoc, true});
  auto Member = pointerExpr(ExprKind::Member, intType(), {variable("nct_object", Record)});
  Member.Name = "nct_value";
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Member;
  Assign.Value = literal("4");
  M.Functions[0].Body = {label(), Assign, ret(Member)};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static nct_record nct_object = {"), std::string::npos);
  auto Bad = M;
  Bad.Globals[0].Mutable = false;
  invalid(Bad, "Global constants");
  Bad = M;
  Bad.Globals[0].Value = literal("0", Record);
  invalid(Bad, "literal");
  Bad = M;
  Bad.Globals[0].Value.Args.clear();
  invalid(Bad, "operand count");
  Bad = M;
  Bad.Globals[0].Value.Args[0] = binary(BinaryOperator::Add, literal("1"), literal("2"));
  invalid(Bad, "folded");
  M.Functions[0].Body[1].Target = variable("nct_object", Record);
  M.Functions[0].Body[1].Value = Value;
  D.clear();
  ASSERT_TRUE(verifyModule(M, context(M), D));
  M.Profile = "cpp-core-v1";
  M.Target.Carriers.reset();
  M.Records[0].Layout.reset();
  invalid(M, "Mutable globals require core v2");
}

TEST(TranslateIR, CoreV2MutableStaticRecordSelfAddressesRequireExactLayoutAndStorage) {
  auto M = module(true);
  const Type Record{TypeKind::Record, "nct_record"};
  const auto Pointer = pointerType(intType());
  M.Records.push_back({"nct_record", {{"nct_value", intType()}, {"nct_pointer", Pointer}},
                       InputLoc, RecordLayout{{128, 64}, {0, 64}}});
  auto Member = pointerExpr(ExprKind::Member, intType(), {variable("nct_object", Record)});
  Member.Name = "nct_value";
  auto Address = pointerExpr(ExprKind::Address, Pointer, {Member});
  M.Globals.push_back({"nct_object", Record,
      pointerExpr(ExprKind::Aggregate, Record, {literal("3"), Address}), InputLoc, true});
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  auto Declaration = Output.Text.find("static nct_record nct_object;");
  auto Initializer = Output.Text.find("static nct_record nct_object = {");
  ASSERT_NE(Declaration, std::string::npos);
  ASSERT_NE(Initializer, std::string::npos);
  EXPECT_LT(Declaration, Initializer);
  auto Bad = M;
  Bad.Globals[0].Value.Args[1].Args[0].Name = "missing";
  invalid(Bad, "Unknown member");
  Bad = M;
  Bad.Records[0].Layout->FieldOffsetsBits[1] = 32;
  invalid(Bad, "layout");
}

TEST(TranslateIR, CoreV2StaticReferenceCarriersKeepBindingReadonlyAndPointeeWritable) {
  auto M = module(true);
  const auto Pointer = pointerType(intType());
  M.Globals.push_back({"nct_alias", Pointer,
      pointerExpr(ExprKind::Address, Pointer, {variable("nct_target", intType())}), InputLoc});
  M.Globals.push_back({"nct_target", intType(), literal("3"), InputLoc, true});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = pointerExpr(ExprKind::Dereference, intType(), {variable("nct_alias", Pointer)});
  Assign.Value = literal("4");
  M.Functions[0].Body = {label(), Assign, ret(variable("nct_target", intType()))};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static int *const nct_alias = (&(nct_target));"), std::string::npos);
  auto Bad = M;
  Bad.Functions[0].Body[1].Target = variable("nct_alias", Pointer);
  Bad.Functions[0].Body[1].Value = pointerExpr(ExprKind::Null, Pointer);
  invalid(Bad, "Global constants");
  const auto ConstPointer = pointerType(intType(), true);
  Bad = M;
  Bad.Globals[0].ValueType = ConstPointer;
  Bad.Globals[0].Value.ValueType = ConstPointer;
  Bad.Globals[1].Mutable = false;
  Bad.Functions[0].Body[1].Target->Args[0].ValueType = ConstPointer;
  invalid(Bad, "Const pointee");
  Bad.Functions[0].Body.erase(Bad.Functions[0].Body.begin() + 1);
  D.clear();
  ASSERT_TRUE(verifyModule(Bad, context(Bad), D));
}

TEST(TranslateIR, CoreV2StaticPointersDeclareForwardObjectsAndRetainStoragePermissions) {
  auto M = module(true);
  const auto Pointer = pointerType(intType());
  auto Address = pointerExpr(ExprKind::Address, Pointer, {variable("nct_target", intType())});
  M.Globals.push_back({"nct_pointer", Pointer, Address, InputLoc, true});
  M.Globals.push_back({"nct_target", intType(), literal("3"), InputLoc, true});
  auto Pointee = pointerExpr(ExprKind::Dereference, intType(), {variable("nct_pointer", Pointer)});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Pointee;
  Assign.Value = literal("4");
  M.Functions[0].Body = {label(), Assign, ret(Pointee)};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  auto Declaration = Output.Text.find("static int nct_target;");
  auto Initializer = Output.Text.find("static int * nct_pointer = (&(nct_target));");
  ASSERT_NE(Declaration, std::string::npos);
  ASSERT_NE(Initializer, std::string::npos);
  EXPECT_LT(Declaration, Initializer);
  auto Bad = M;
  Bad.Globals[1].Mutable = false;
  invalid(Bad, "Global constants");
  Bad = M;
  Bad.Globals[0].Value = variable("nct_pointer", Pointer);
  invalid(Bad, "cannot load storage");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Name = "nct_missing";
  invalid(Bad, "defined global object");
  Bad = M;
  Bad.Globals[0].Value = pointerExpr(ExprKind::Cast, Pointer, {literal("1")});
  invalid(Bad, "bounded pointer expression");
  Bad = M;
  Bad.Globals[0].Value.Args[0] = literal("3");
  invalid(Bad, "global objects");
  Bad = M;
  Bad.Globals[0].Value = pointerExpr(ExprKind::Null, Pointer);
  D.clear();
  ASSERT_TRUE(verifyModule(Bad, context(Bad), D));
  Bad.Profile = "cpp-core-v1";
  Bad.Target.Carriers.reset();
  invalid(Bad, "Mutable globals require core v2");
}

TEST(TranslateIR, CoreV2StaticPointerOffsetsStayConstantAndRespectOnePastBounds) {
  auto M = module(true);
  const auto Array = arrayType(intType(), 3);
  const auto Pointer = pointerType(intType());
  auto Values = pointerExpr(ExprKind::Aggregate, Array,
                            {literal("1"), literal("2"), literal("3")});
  auto Decay = pointerExpr(ExprKind::ArrayDecay, Pointer, {variable("nct_array", Array)});
  auto Place = pointerExpr(ExprKind::Index, intType(), {Decay, literal("3")});
  M.Globals.push_back({"nct_end", Pointer,
      pointerExpr(ExprKind::Address, Pointer, {Place}), InputLoc, true});
  M.Globals.push_back({"nct_array", Array, Values, InputLoc, true});
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  auto Start = Output.Text.find("static int * nct_end = ");
  ASSERT_NE(Start, std::string::npos);
  auto Line = Output.Text.substr(Start, Output.Text.find('\n', Start) - Start);
  EXPECT_EQ(Line.find("nct_emit_"), std::string::npos);
  EXPECT_NE(Line.find(" + "), std::string::npos);
  auto Bad = M;
  Bad.Globals[0].Value.Args[0].Args[1] = literal("4");
  invalid(Bad, "index exceeds");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Args[1] = literal("-1");
  invalid(Bad, "nonnegative integer index");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Args[0] = variable("nct_end", Pointer);
  invalid(Bad, "nonnegative integer index");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Args[1] = variable("nct_array", Array);
  invalid(Bad, "nonnegative integer index");
  Bad = M;
  Bad.Globals[0].Value = binary(BinaryOperator::Add,
                               variable("nct_end", Pointer), literal("0"), Pointer);
  invalid(Bad, "cannot load storage");
  // Advancing the complete array object by one is also a valid constant.
  const auto WholePointer = pointerType(Array);
  auto Whole = pointerExpr(ExprKind::Address, WholePointer, {variable("nct_array", Array)});
  M.Globals[0].ValueType = WholePointer;
  M.Globals[0].Value = pointerExpr(ExprKind::Address, WholePointer, {
      pointerExpr(ExprKind::Index, Array, {Whole, literal("1")})});
  D.clear();
  ASSERT_TRUE(verifyModule(M, context(M), D));
}

TEST(TranslateIR, CoreV2StaticPointerSubobjectsExcludeDereferencedLoadsAndPastMembers) {
  auto M = module(true);
  const Type Record{TypeKind::Record, "nct_record"};
  M.Records.push_back({"nct_record", {{"value", intType()}}, InputLoc,
                       RecordLayout{{32, 32}, {0}}});
  const auto Array = arrayType(Record, 2);
  auto R = pointerExpr(ExprKind::Aggregate, Record, {literal("3")});
  M.Globals.push_back({"nct_array", Array,
      pointerExpr(ExprKind::Aggregate, Array, {R, R}), InputLoc});
  auto Index = pointerExpr(ExprKind::Index, Record, {
      pointerExpr(ExprKind::ArrayDecay, pointerType(Record, true), {variable("nct_array", Array)}),
      literal("1")});
  auto Member = pointerExpr(ExprKind::Member, intType(), {Index});
  Member.Name = "value";
  const auto Pointer = pointerType(intType(), true);
  M.Globals.push_back({"nct_pointer", Pointer,
      pointerExpr(ExprKind::Address, Pointer, {Member}), InputLoc});
  Diagnostics D;
  ASSERT_TRUE(verifyModule(M, context(M), D));
  auto Bad = M;
  Bad.Globals[1].Value.Args[0].Args[0].Args[1] = literal("2");
  invalid(Bad, "subobject of one-past");
  Bad = M;
  Bad.Globals[1].Value.Args[0].Name = "missing";
  invalid(Bad, "Unknown member");
  Bad = M;
  Bad.Globals[1].Value.Args[0] = pointerExpr(ExprKind::Dereference, intType(), {
      variable("nct_pointer", Pointer)});
  invalid(Bad, "global objects");
  Bad = M;
  Bad.Globals[1].ValueType = pointerType(intType());
  Bad.Globals[1].Value.ValueType = pointerType(intType());
  invalid(Bad, "Const pointee");
  // Explicit source casts can preserve an address while changing qualifiers.
  Bad.Globals[1].Value = pointerExpr(ExprKind::Cast, pointerType(intType()), {M.Globals[1].Value});
  D.clear();
  ASSERT_TRUE(verifyModule(Bad, context(Bad), D));
}

TEST(TranslateIR, CoreV2StaticPointerWireRetainsSymbolicAddresses) {
  auto JSON = wireModule(true);
  const std::string Globals = R"json([
    {"name":"nct_pointer","type":"ptr:int","mutable":true,
     "value":{"kind":"address","type":"ptr:int","args":[
       {"kind":"var","name":"nct_target","type":"int","loc":{"file":"input.cpp","line":1,"column":1}}
     ],"loc":{"file":"input.cpp","line":1,"column":1}},
     "loc":{"file":"input.cpp","line":1,"column":1}},
    {"name":"nct_target","type":"int","mutable":true,
     "value":{"kind":"literal","type":"int","value":"3","loc":{"file":"input.cpp","line":1,"column":1}},
     "loc":{"file":"input.cpp","line":1,"column":1}}
  ])json";
  replaceOnce(JSON, "\"globals\": []", "\"globals\": " + Globals);
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(JSON, M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  auto Bad = JSON;
  replaceOnce(Bad, "\"name\":\"nct_target\",\"type\":\"int\",\"loc\"",
                   "\"name\":\"nct_missing\",\"type\":\"int\",\"loc\"");
  ASSERT_TRUE(parseModule(Bad, M, D));
  invalid(M, "defined global object");
}

TEST(TranslateIR, CoreV2MutableStaticArraysPreserveStoresAndReadonlyAliases) {
  auto M = module(true);
  const auto Array = arrayType(intType(), 2);
  const auto Pointer = pointerType(intType());
  auto Values = pointerExpr(ExprKind::Aggregate, Array, {literal("1"), literal("0")});
  M.Globals.push_back({"nct_array", Array, Values, InputLoc, true});
  auto Decay = pointerExpr(ExprKind::ArrayDecay, Pointer, {variable("nct_array", Array)});
  auto Element = pointerExpr(ExprKind::Index, intType(), {Decay, literal("1")});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = InputLoc;
  Assign.Target = Element;
  Assign.Value = literal("3");
  M.Functions[0].Body = {label(), Assign, ret(Element)};
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static int nct_array[2] = {"), std::string::npos);
  auto Bad = M;
  Bad.Globals[0].Mutable = false;
  invalid(Bad, "Global constants");
  Bad = M;
  Bad.Functions[0].Body[1].Target->Args[0].ValueType = pointerType(intType(), true);
  invalid(Bad, "Const pointee");
  Bad = M;
  Bad.Globals[0].Value = literal("0", Array);
  invalid(Bad, "literal");
  Bad = M;
  Bad.Globals[0].Value.Args.pop_back();
  invalid(Bad, "operand count");
  Bad = M;
  Bad.Globals[0].Value.Args[0] = binary(BinaryOperator::Add, literal("1"), literal("2"));
  invalid(Bad, "folded");
  Bad = M;
  Bad.Functions[0].Body[1].Target = variable("nct_array", Array);
  Bad.Functions[0].Body[1].Value = Values;
  invalid(Bad, "Array aggregate");
  M.Functions[0].Result = pointerType(intType(), true);
  Decay.ValueType = M.Functions[0].Result;
  M.Functions[0].Body = {label(), ret(Decay)};
  D.clear();
  ASSERT_TRUE(verifyModule(M, context(M), D));
}

TEST(TranslateIR, CoreV2MutableStaticArrayTreesRetainNestedStorage) {
  auto M = module(true);
  const auto Row = arrayType(intType(), 2);
  const Type Record{TypeKind::Record, "nct_record"};
  M.Records.push_back({"nct_record", {{"values", Row}}, InputLoc,
                       RecordLayout{{64, 32}, {0}}});
  const auto Array = arrayType(Record, 2);
  auto RecordValue = pointerExpr(ExprKind::Aggregate, Record, {
      pointerExpr(ExprKind::Aggregate, Row, {literal("1"), literal("0")})});
  auto Values = pointerExpr(ExprKind::Aggregate, Array, {RecordValue, RecordValue});
  M.Globals.push_back({"nct_array", Array, Values, InputLoc, true});
  auto Element = pointerExpr(ExprKind::Index, Record, {
      pointerExpr(ExprKind::ArrayDecay, pointerType(Record), {variable("nct_array", Array)}),
      literal("1")});
  auto Member = pointerExpr(ExprKind::Member, Row, {Element});
  Member.Name = "values";
  M.Functions[0].Result = pointerType(intType());
  M.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::ArrayDecay, M.Functions[0].Result, {Member}));
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static nct_record nct_array[2] = {"), std::string::npos);
  auto Bad = M;
  Bad.Globals[0].Value.Args[1].Args[0].Args.pop_back();
  invalid(Bad, "operand count");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Args[0].Args[1] = literal("1", uintType());
  invalid(Bad, "Array element initializer type mismatch");
  Bad = M;
  Bad.Profile = "cpp-core-v1";
  Bad.Target.Carriers.reset();
  invalid(Bad, "Array types require core v2");
}

TEST(TranslateIR, CoreV2ConstantArraysHaveStaticConstStorageAndCheckedAddresses) {
  auto M = module(true);
  const auto Byte = integerType(8);
  const auto Array = arrayType(Byte, 4);
  const auto Pointer = pointerType(Byte, true);
  auto Value = pointerExpr(ExprKind::Aggregate, Array,
                           {literal("-1", Byte), literal("0", Byte),
                            literal("97", Byte), literal("0", Byte)});
  M.Globals.push_back({"nct_string", Array, Value, InputLoc});
  M.Functions[0].Result = Pointer;
  M.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::ArrayDecay, Pointer, {variable("nct_string", Array)}));
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static const signed char nct_string[4] = {"),
            std::string::npos);
  auto Bad = M;
  Bad.Functions[0].Result = pointerType(Byte);
  Bad.Functions[0].Body.back().Value->ValueType = pointerType(Byte);
  invalid(Bad, "Global constants");
  Bad = M;
  Bad.Globals[0].Value.Args.pop_back();
  invalid(Bad, "operand count");
  Bad = M;
  Bad.Globals[0].Value.Args[0] = binary(BinaryOperator::Add, literal("1"), literal("2"));
  invalid(Bad, "folded");
  Bad = M;
  Bad.Globals[0].Value.Args[0] = literal("1");
  invalid(Bad, "Array element initializer type mismatch");
  Bad = M;
  Bad.Profile = "cpp-core-v1";
  Bad.Target.Carriers.reset();
  invalid(Bad, "Array types require core v2");
}

TEST(TranslateIR, CoreV2ConstantArrayTreesRetainLayoutAndConstSubobjects) {
  auto M = module(true);
  const auto Row = arrayType(intType(), 2);
  const auto Array = arrayType(Row, 2);
  const Type Record{TypeKind::Record, "nct_record"};
  auto Values = pointerExpr(ExprKind::Aggregate, Array, {
      pointerExpr(ExprKind::Aggregate, Row, {literal("1"), literal("0")}),
      pointerExpr(ExprKind::Aggregate, Row, {literal("2"), literal("3")})});
  M.Records.push_back({"nct_record", {{"values", Array}}, InputLoc,
                       RecordLayout{{128, 32}, {0}}});
  M.Globals.push_back({"nct_constant", Record,
      pointerExpr(ExprKind::Aggregate, Record, {Values}), InputLoc});
  auto Member = pointerExpr(ExprKind::Member, Array, {variable("nct_constant", Record)});
  Member.Name = "values";
  M.Functions[0].Result = pointerType(Row, true);
  M.Functions[0].Body.back() = ret(pointerExpr(
      ExprKind::ArrayDecay, M.Functions[0].Result, {Member}));
  Diagnostics D;
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("static const nct_record nct_constant = {"), std::string::npos);
  EXPECT_NE(Output.Text.find("sizeof(nct_record) * __CHAR_BIT__ == 128"), std::string::npos);
  auto Bad = M;
  Bad.Functions[0].Result = pointerType(Row);
  Bad.Functions[0].Body.back().Value->ValueType = pointerType(Row);
  invalid(Bad, "Global constants");
  Bad = M;
  Bad.Records[0].Layout->Storage.SizeBits = 96;
  invalid(Bad, "target layout");
  Bad = M;
  Bad.Globals[0].Value.Args[0].Args[1].Args.pop_back();
  invalid(Bad, "operand count");
}

TEST(TranslateIR, CoreV2ConstantArrayWireRejectsIncompleteAndRuntimeValues) {
  const std::string Global = R"json({"name":"nct_string","type":"arr:2:i8",
    "value":{"kind":"aggregate","type":"arr:2:i8","args":[
      {"kind":"literal","type":"i8","value":"97","loc":{"file":"input.cpp","line":1,"column":1}},
      {"kind":"literal","type":"i8","value":"0","loc":{"file":"input.cpp","line":1,"column":1}}
    ],"loc":{"file":"input.cpp","line":1,"column":1}},
    "loc":{"file":"input.cpp","line":1,"column":1}})json";
  auto Wire = [&](const std::string &G) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"globals\": []", "\"globals\": [" + G + "]");
    return JSON;
  };
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(Wire(Global), M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  auto Bad = Global;
  replaceOnce(Bad, "\"value\":\"97\"", "\"value\":\"256\"");
  ASSERT_TRUE(parseModule(Wire(Bad), M, D));
  invalid(M, "literal value or range");
  Bad = Global;
  replaceOnce(Bad, "\"kind\":\"literal\",\"type\":\"i8\",\"value\":\"97\"",
              "\"kind\":\"var\",\"type\":\"i8\",\"name\":\"nct_external\"");
  ASSERT_TRUE(parseModule(Wire(Bad), M, D));
  invalid(M, "Global initializer");
}

TEST(TranslateIR, CoreV2FloatingLiteralsPreserveExactBitsAndGuards) {
  struct Case { TypeKind Kind; uint64_t Bits; const char *Text; };
  for (const auto &C : std::vector<Case>{
           {TypeKind::Float, 0, "0x0p+0f"},
           {TypeKind::Float, 0x80000000, "(-0x0p+0f)"},
           {TypeKind::Float, 1, "0x0.000002p-126f"},
           {TypeKind::Float, 0x007fffff, "0x0.fffffep-126f"},
           {TypeKind::Float, 0x00800000, "0x1.000000p-126f"},
           {TypeKind::Float, 0x3fc00000, "0x1.800000p+0f"},
           {TypeKind::Float, 0x7f7fffff, "0x1.fffffep+127f"},
           {TypeKind::Float, 0x7f800000, "__builtin_huge_valf()"},
           {TypeKind::Float, 0xff800000, "(-__builtin_huge_valf())"},
           {TypeKind::Float, 0x7fc01234, "__builtin_nanf(\"0x001234\")"},
           {TypeKind::Float, 0x7f801234, "__builtin_nansf(\"0x001234\")"},
           {TypeKind::Double, UINT64_C(0x8000000000000000), "(-0x0p+0)"},
           {TypeKind::Double, 1, "0x0.0000000000001p-1022"},
           {TypeKind::Double, UINT64_C(0x3ff8000000000000), "0x1.8000000000000p+0"}}) {
    SCOPED_TRACE(C.Text);
    auto M = module(true);
    M.Functions[0].Result = {C.Kind, {}};
    auto Value = pointerExpr(ExprKind::Literal, M.Functions[0].Result);
    Value.Binary64Bits = C.Bits;
    M.Functions[0].Body.back() = ret(Value);
    Diagnostics D;
    EmittedSource Output;
    ASSERT_TRUE(emitNC(M, context(M), Output, D));
    EXPECT_NE(Output.Text.find(C.Text), std::string::npos);
    EXPECT_NE(Output.Text.find("#pragma STDC FP_CONTRACT OFF"), std::string::npos);
    EXPECT_NE(Output.Text.find("__FLT_EVAL_METHOD__ == 0"), std::string::npos);
    EXPECT_NE(Output.Text.find("__FAST_MATH__"), std::string::npos);
    EXPECT_NE(Output.Text.find("sizeof(float) * __CHAR_BIT__ == 32"), std::string::npos);
    EXPECT_NE(Output.Text.find("sizeof(double) * __CHAR_BIT__ == 64"), std::string::npos);
    M.Profile = "cpp-core-v1";
    M.Target.Carriers.reset();
    invalid(M, C.Kind == TypeKind::Float ? "Float requires" : "Double requires");
  }
}

TEST(TranslateIR, CoreV2FloatingArithmeticRequiresSourceConversions) {
  for (auto Kind : {TypeKind::Float, TypeKind::Double}) {
    Type T{Kind, {}};
    auto Value = pointerExpr(ExprKind::Literal, T);
    Value.Binary64Bits = Kind == TypeKind::Float ? UINT64_C(0x3f800000)
                                               : UINT64_C(0x3ff0000000000000);
    for (auto Op : {BinaryOperator::Add, BinaryOperator::Subtract,
                   BinaryOperator::Multiply, BinaryOperator::Divide,
                   BinaryOperator::Equal, BinaryOperator::NotEqual,
                   BinaryOperator::Less, BinaryOperator::LessEqual,
                   BinaryOperator::Greater, BinaryOperator::GreaterEqual}) {
      auto M = module(true);
      const auto Result = Op >= BinaryOperator::Equal ? boolType() : T;
      M.Functions[0].Result = Result;
      M.Functions[0].Body.back() = ret(binary(Op, Value, Value, Result));
      Diagnostics D;
      ASSERT_TRUE(verifyModule(M, context(M), D));
      M.Functions[0].Body.back().Value->Args[1] = literal("1");
      invalid(M, "matching converted types");
    }
    for (auto Op : {BinaryOperator::Remainder, BinaryOperator::BitAnd,
                   BinaryOperator::BitOr, BinaryOperator::BitXor,
                   BinaryOperator::ShiftLeft, BinaryOperator::ShiftRight}) {
      auto M = module(true);
      M.Functions[0].Result = T;
      M.Functions[0].Body.back() = ret(binary(Op, Value, Value, T));
      invalid(M, "matching converted types");
    }
    for (auto To : {intType(), uintType(), boolType(), Type{TypeKind::Float, {}},
                    Type{TypeKind::Double, {}}, integerType(64, true), integerType(64, false)}) {
      auto M = module(true);
      M.Functions[0].Result = To;
      M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Cast, To, {Value}));
      Diagnostics D;
      EXPECT_TRUE(verifyModule(M, context(M), D));
    }
    auto M = module(true);
    M.Functions[0].Result = T;
    Value.Integer = "1";
    M.Functions[0].Body.back() = ret(Value);
    invalid(M, "invalid payload");
  }
  auto M = module(true);
  M.Functions[0].Result = {TypeKind::Float, {}};
  auto Value = pointerExpr(ExprKind::Literal, M.Functions[0].Result);
  Value.Binary64Bits = UINT64_C(0x100000000);
  M.Functions[0].Body.back() = ret(Value);
  invalid(M, "invalid payload or width");
}

TEST(TranslateIR, CoreV2FloatingLayoutAndMutableStorageAreIndependent) {
  auto M = module(true);
  const Type F32{TypeKind::Float, {}}, F64{TypeKind::Double, {}};
  M.Records.push_back({"nct_box", {{"values", arrayType(F32, 3)}, {"wide", F64}},
                       InputLoc, RecordLayout{{192, 64}, {0, 128}}});
  auto Value = pointerExpr(ExprKind::Literal, F32);
  Value.Binary64Bits = 0x3fc00000;
  M.Globals.push_back({"nct_global", F32, Value, InputLoc, true});
  M.Functions[0].Result = F32;
  M.Functions[0].Body.back() = ret(variable("nct_global", F32));
  Diagnostics D;
  ASSERT_TRUE(verifyModule(M, context(M), D));
  auto Bad = M;
  Bad.Target.Carriers->Carriers[10].ABIAlignBits = 16;
  invalid(Bad, "layouts disagree");
  Bad = M;
  Bad.Target.Carriers->Carriers[11].SizeBits = 32;
  invalid(Bad, "layouts disagree");
  Bad = M;
  Bad.Records[0].Layout->FieldOffsetsBits[1] = 96;
  invalid(Bad, "layout");
}

TEST(TranslateIR, CoreV2FloatingWireRequiresCanonicalBitsAndPayload) {
  for (const auto *Type : {"float", "double"}) {
    const std::string Good = std::string(Type) == "float" ? "3fc00000" : "3ff8000000000000";
    auto Wire = [&](const std::string &Bits, const std::string &Extra = "") {
      auto JSON = wireModule(true);
      replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + std::string(Type) + "\"");
      replaceOnce(JSON, "\"type\": \"bool\", \"value\": true",
                  "\"type\": \"" + std::string(Type) + "\", \"bits\": \"" + Bits + "\"" + Extra);
      return JSON;
    };
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(Wire(Good), M, D));
    ASSERT_TRUE(verifyModule(M, context(M), D));
    for (const auto &Bits : {Good + "0", Good.substr(1), std::string("+1"),
                             std::string("0x") + Good, std::string(Good.size(), 'F')}) {
      D.clear();
      EXPECT_FALSE(parseModule(Wire(Bits), M, D));
      ASSERT_FALSE(D.empty());
    }
    for (const auto *Extra : {",\"value\":\"1\"", ",\"args\":[]", ",\"name\":\"nct_x\""}) {
      D.clear();
      EXPECT_FALSE(parseModule(Wire(Good, Extra), M, D));
    }
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
  invalid(M, "mutable storage");
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

namespace {
Type functionPointerType(Type Result = intType(),
                         std::vector<Type> Parameters = {}) {
  Type T{TypeKind::FunctionPointer, {}, {std::move(Result)}};
  T.Elements.insert(T.Elements.end(), Parameters.begin(), Parameters.end());
  return T;
}
Expr functionAddress(std::string Name, Type T) {
  auto E = pointerExpr(ExprKind::FunctionAddress, std::move(T));
  E.Name = std::move(Name);
  return E;
}
Module callbackModule() {
  auto M = module(true);
  auto T = functionPointerType(intType(), {intType()});
  M.Functions[0].Params = {{"nct_x", intType(), InputLoc}};
  M.Functions[0].Body.back() = ret(variable("nct_x"));
  Function F;
  F.Name = "nct_apply";
  F.Result = intType();
  F.Internal = true;
  F.Loc = InputLoc;
  F.Params = {{"nct_cb", T, InputLoc}};
  F.Locals = {{"nct_result", intType(), InputLoc}};
  Instruction I;
  I.Op = InstructionKind::IndirectCall;
  I.Loc = InputLoc;
  I.Callable = variable("nct_cb", T);
  I.Args = {literal("7")};
  I.Target = variable("nct_result");
  F.Body = {label(), I, ret(variable("nct_result"))};
  M.Functions.push_back(std::move(F));
  M.Globals = {{"nct_callback", T, functionAddress("sample", T), InputLoc}};
  return M;
}
}

TEST(TranslateIR, CoreV2NullPtrWireRetainsDistinctScalarType) {
  auto JSON = wireModule(true);
  replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"nullptr\"");
  replaceOnce(JSON, "\"kind\": \"literal\", \"type\": \"bool\", \"value\": true",
              "\"kind\": \"null\", \"type\": \"nullptr\"");
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(JSON, M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  EXPECT_EQ(M.Functions[0].Result.Kind, TypeKind::NullPtr);
  EXPECT_EQ(M.Exports[0].Result, "nullptr");
  EXPECT_EQ(typeName(M.Functions[0].Result), "nullptr");
  EXPECT_NE(M.Functions[0].Result, pointerType({TypeKind::Void, {}}));
  EmittedSource Out;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  EXPECT_NE(Out.Text.find("typeof(nullptr) sample(void)"), std::string::npos);
  EXPECT_NE(Out.Text.find("return nullptr;"), std::string::npos);
  EXPECT_NE(Out.Text.find("sizeof(typeof(nullptr)) * __CHAR_BIT__ == 64"), std::string::npos);
  EXPECT_NE(Out.Text.find("alignof(typeof(nullptr)) * __CHAR_BIT__ == 64"), std::string::npos);
  M.Profile = "cpp-core-v1";
  M.Target.Carriers.reset();
  invalid(M, "Null pointer values require core v2");
}

TEST(TranslateIR, CoreV2NullPtrStorageComposesWithDeclaratorsAndLayout) {
  Type T{TypeKind::NullPtr, {}};
  auto M = module(true);
  M.Globals = {{"nct_fixed", T, pointerExpr(ExprKind::Null, T), InputLoc},
               {"nct_mutable", T, pointerExpr(ExprKind::Null, T), InputLoc, true}};
  Record R{"nct_record", {{"nct_value", T},
                          {"nct_values", arrayType(T, 2)}}, InputLoc,
           RecordLayout{{192, 64}, {0, 64}}};
  M.Records.push_back(R);
  M.Functions[0].Locals = {{"nct_ref", pointerType(T, true), InputLoc},
                           {"nct_array", arrayType(T, 2), InputLoc},
                           {"nct_callback", functionPointerType(T, {T}), InputLoc},
                           {"nct_object", {TypeKind::Record, R.ID}, InputLoc}};
  EmittedSource Out;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  EXPECT_NE(Out.Text.find("static const typeof(nullptr) nct_fixed = nullptr;"), std::string::npos);
  EXPECT_NE(Out.Text.find("static typeof(nullptr) nct_mutable = nullptr;"), std::string::npos);
  EXPECT_NE(Out.Text.find("const typeof(nullptr) * nct_ref;"), std::string::npos);
  EXPECT_NE(Out.Text.find("typeof(nullptr) nct_array[2];"), std::string::npos);
  EXPECT_NE(Out.Text.find("typeof(nullptr) (* nct_callback)(typeof(nullptr));"), std::string::npos);
  EXPECT_NE(Out.Text.find("typeof(nullptr) nct_values[2];"), std::string::npos);
  M.Records[0].Layout->FieldOffsetsBits[1] = 32;
  invalid(M, "field offset");
}

TEST(TranslateIR, CoreV2NullPtrEqualityAndBoolConversionRemainTyped) {
  Type T{TypeKind::NullPtr, {}};
  const auto Null = pointerExpr(ExprKind::Null, T);
  auto Negated = pointerExpr(ExprKind::Unary, boolType(), {Null});
  Negated.UnaryOp = UnaryOperator::LogicalNot;
  for (auto E : {binary(BinaryOperator::Equal, Null, Null, boolType()),
                 binary(BinaryOperator::NotEqual, Null, Null, boolType()),
                 pointerExpr(ExprKind::Cast, boolType(), {Null}), Negated}) {
    auto M = module(true);
    M.Functions[0].Result = boolType();
    M.Functions[0].Body.back() = ret(E);
    Diagnostics D;
    EmittedSource Out;
    EXPECT_TRUE(emitNC(M, context(M), Out, D));
  }
  auto M = module(true);
  M.Functions[0].Result = T;
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Cast, T, {Null}));
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
}

TEST(TranslateIR, CoreV2NullPtrRejectsForgedValueTypesAndPayloads) {
  Type T{TypeKind::NullPtr, {}};
  for (unsigned Case = 0; Case < 5; ++Case) {
    auto Bad = T;
    if (Case == 0) Bad.RecordID = "nct_record";
    if (Case == 1) Bad.Elements = {intType()};
    if (Case == 2) Bad.Count = 1;
    if (Case == 3) Bad.PointeeConst = true;
    if (Case == 4) Bad.IntegerBits = 64;
    auto M = module(true);
    M.Functions[0].Result = Bad;
    M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Null, Bad));
    invalid(M);
  }
  for (auto NullType : {T, pointerType(intType()), functionPointerType()}) {
    for (unsigned Case = 0; Case < 7; ++Case) {
      auto E = pointerExpr(ExprKind::Null, NullType);
      if (Case == 0) E.Args = {literal("0")};
      if (Case == 1) E.Integer = "0";
      if (Case == 2) E.Name = "nct_hidden";
      if (Case == 3) E.Boolean = true;
      if (Case == 4) E.Binary64Bits = 1;
      if (Case == 5) E.UnaryOp = UnaryOperator::Minus;
      if (Case == 6) E.BinaryOp = BinaryOperator::Equal;
      auto M = module(true);
      M.Functions[0].Result = NullType;
      M.Functions[0].Body.back() = ret(E);
      invalid(M);
    }
  }
  auto M = module(true);
  M.Functions[0].Result = T;
  M.Functions[0].Body.back() = ret(literal("0", T));
  invalid(M, "Invalid scalar literal");
  M = module(true);
  M.Globals = {{"nct_null", T, pointerExpr(ExprKind::Null, T), InputLoc},
               {"nct_alias", T, variable("nct_null", T), InputLoc}};
  invalid(M, "Global initializer");
}

TEST(TranslateIR, CoreV2NullPtrRejectsInventedArithmeticAndCasts) {
  Type T{TypeKind::NullPtr, {}};
  const auto Null = pointerExpr(ExprKind::Null, T);
  for (auto E : {binary(BinaryOperator::Add, Null, literal("1"), T),
                 binary(BinaryOperator::Less, Null, Null, boolType()),
                 binary(BinaryOperator::Equal, Null, literal("0"), boolType()),
                 binary(BinaryOperator::Equal, Null, Null, intType()),
                 pointerExpr(ExprKind::Cast, intType(), {Null}),
                 pointerExpr(ExprKind::Cast, pointerType(intType()), {Null}),
                 pointerExpr(ExprKind::Cast, T, {literal("0")}),
                 pointerExpr(ExprKind::Cast, T, {pointerExpr(ExprKind::Null, pointerType(intType()))})}) {
    auto M = module(true);
    M.Functions[0].Result = E.ValueType;
    M.Functions[0].Body.back() = ret(E);
    invalid(M);
  }
}

TEST(TranslateIR, CoreV2FunctionPointerWireGrammarRoundTripsNestedSignatures) {
  auto T = functionPointerType();
  EXPECT_EQ(typeName(T), "fnptr:0:3:int");
  EXPECT_EQ(typeName(functionPointerType({TypeKind::Void, {}})), "fnptr:0:4:void");
  EXPECT_EQ(typeName(functionPointerType(intType(), {intType()})), "fnptr:1:3:int3:int");
  for (auto Type : {T, functionPointerType(T, {pointerType(T, true)}),
                    functionPointerType(intType(), std::vector<neverc::translate::Type>(64, T))}) {
    auto JSON = wireModule(true);
    auto Spelling = typeName(Type);
    replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + Spelling + "\"");
    replaceOnce(JSON, "\"kind\": \"literal\", \"type\": \"bool\", \"value\": true",
                 "\"kind\": \"null\", \"type\": \"" + Spelling + "\"");
    Module M;
    Diagnostics D;
    ASSERT_TRUE(parseModule(JSON, M, D)) << Spelling;
    ASSERT_TRUE(verifyModule(M, context(M), D)) << Spelling;
    EXPECT_EQ(M.Functions[0].Result, Type);
    EXPECT_EQ(typeName(M.Functions[0].Result), Spelling);
  }
}

TEST(TranslateIR, CoreV2FunctionPointerWireRejectsAmbiguousAndUnboundedTypes) {
  std::vector<std::string> Bad = {
      "fnptr:", "fnptr:00:3:int", "fnptr:+0:3:int", "fnptr:65:3:int",
      "fnptr:4294967296:3:int", "fnptr:0:0:", "fnptr:0:03:int",
      "fnptr:0:+3:int", "fnptr:0:4:int", "fnptr:0:3:intx",
      "fnptr:1:3:int", "fnptr:0:3:int3:int", "fnptr:0:4294967296:int",
      "fnptr:1:3:int6:ptr:", "fnptr:0:7:fnptr:?"};
  std::string Deep = "int";
  for (unsigned I = 0; I < 66; ++I)
    Deep = "fnptr:0:" + std::to_string(Deep.size()) + ":" + Deep;
  Bad.push_back(Deep);
  for (const auto &Spelling : Bad) {
    auto JSON = wireModule(true);
    replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"" + Spelling + "\"");
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D)) << Spelling;
  }
}

TEST(TranslateIR, CoreV2FunctionPointerSyntheticTypesCannotBypassSignatureRules) {
  auto Valid = functionPointerType();
  std::vector<Type> Bad = {
      {TypeKind::FunctionPointer, {}},
      functionPointerType(intType(), std::vector<Type>(65, intType())),
      functionPointerType(intType(), {{TypeKind::Void, {}}}),
      functionPointerType(arrayType(intType(), 2)),
      functionPointerType(intType(), {arrayType(intType(), 2)}),
      functionPointerType({TypeKind::Record, "nct_record"}),
      functionPointerType({TypeKind::Double, "nct_invalid"})};
  for (unsigned Field = 0; Field < 4; ++Field) {
    auto T = Valid;
    if (Field == 0) T.RecordID = "nct_record";
    if (Field == 1) T.Count = 1;
    if (Field == 2) T.PointeeConst = true;
    if (Field == 3) T.IntegerBits = 64;
    Bad.push_back(T);
  }
  Type Deep = intType();
  for (unsigned I = 0; I < 66; ++I)
    Deep = functionPointerType(std::move(Deep));
  Bad.push_back(std::move(Deep));
  for (const auto &T : Bad) {
    auto M = module(true);
    M.Functions[0].Result = T;
    M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Null, T));
    invalid(M);
  }
}

TEST(TranslateIR, CoreV2CallbackGlobalsHaveTypedDeclaratorsAndPriorPrototypes) {
  auto M = callbackModule();
  auto T = M.Globals[0].ValueType;
  M.Globals.push_back({"nct_mutable_callback", T, pointerExpr(ExprKind::Null, T), InputLoc, true});
  M.Functions[1].Locals.push_back({"nct_callbacks", arrayType(T, 2), InputLoc});
  M.Functions[1].Locals.push_back({"nct_callback_ref", pointerType(T, true), InputLoc});
  M.Functions[1].Locals.push_back({"nct_callback_factory", functionPointerType(T), InputLoc});
  EmittedSource Out;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  auto Prototype = Out.Text.find("int sample(int nct_x);");
  auto Initializer = Out.Text.find("static int (*const nct_callback)(int) = (&sample);");
  ASSERT_NE(Prototype, std::string::npos);
  ASSERT_NE(Initializer, std::string::npos);
  EXPECT_LT(Prototype, Initializer);
  EXPECT_NE(Out.Text.find("static int (* nct_mutable_callback)(int)"), std::string::npos);
  EXPECT_NE(Out.Text.find("int (* nct_callbacks[2])(int);"), std::string::npos);
  EXPECT_NE(Out.Text.find("int (*const * nct_callback_ref)(int);"), std::string::npos);
  EXPECT_NE(Out.Text.find("int (* (* nct_callback_factory)(void))(int);"), std::string::npos);
  EXPECT_NE(Out.Text.find("sizeof(int (*)(int)) * __CHAR_BIT__ == 64"), std::string::npos);
  EXPECT_NE(Out.Text.find("alignof(int (*)(int)) * __CHAR_BIT__ == 64"), std::string::npos);
  EXPECT_NE(Out.Text.find("nct_result = (nct_cb)((7));"), std::string::npos);
  EXPECT_EQ(Out.Text.find("fnptr:"), std::string::npos);
}

TEST(TranslateIR, CoreV2FunctionAddressesRequireExactDefinedSignatures) {
  for (unsigned Case = 0; Case < 8; ++Case) {
    auto M = callbackModule();
    auto &E = M.Globals[0].Value;
    if (Case == 0) E.Name = "nct_missing";
    if (Case == 1) M.Functions[0].Result = boolType();
    if (Case == 2) M.Functions[0].Params.clear();
    if (Case == 3) M.Functions[0].Params[0].ValueType = uintType();
    if (Case == 4) E.Args = {literal("0")};
    if (Case == 5) E.Integer = "0";
    if (Case == 6) E.Boolean = true;
    if (Case == 7) E.BinaryOp = BinaryOperator::Equal;
    invalid(M);
  }
  auto M = callbackModule();
  M.Globals[0].Value = variable("nct_callback", M.Globals[0].ValueType);
  invalid(M, "Global initializer");
}

TEST(TranslateIR, CoreV2IndirectCallsCheckStorageArgumentsAndResult) {
  for (unsigned Case = 0; Case < 13; ++Case) {
    auto M = callbackModule();
    auto &I = M.Functions[1].Body[1];
    if (Case == 0) I.Callable.reset();
    if (Case == 1) I.Callable = literal("0");
    if (Case == 2) I.Callable->Name = "nct_missing";
    if (Case == 3) I.Callable->ValueType = functionPointerType();
    if (Case == 4) I.Args.clear();
    if (Case == 5) I.Args[0] = literal("7", uintType());
    if (Case == 6) I.Target.reset();
    if (Case == 7) I.Target = variable("nct_cb", M.Functions[1].Params[0].ValueType);
    if (Case == 8) I.Callee = "sample";
    if (Case == 9) I.MappingID = "cpp.math.fabs.f64.v1";
    if (Case == 10) I.Value = literal("0");
    if (Case == 11) I.Condition = literal("0", boolType());
    if (Case == 12) I.Label = "nct_entry";
    invalid(M);
  }
  auto M = callbackModule();
  M.Functions[1].Body[0].Callable = *M.Functions[1].Body[1].Callable;
  invalid(M, "Callable operands");
}

TEST(TranslateIR, CoreV2CallbackVoidAndReferenceCarriersRetainCallContracts) {
  for (auto Result : {Type{TypeKind::Void, {}}, pointerType(intType())}) {
    auto M = module(true);
    auto T = functionPointerType(Result, {pointerType(arrayType(intType(), 2), true)});
    auto &F = M.Functions[0];
    F.Result = Result;
    F.Params = {{"nct_cb", T, InputLoc}, {"nct_array", T.Elements[1], InputLoc}};
    Instruction I;
    I.Op = InstructionKind::IndirectCall;
    I.Loc = InputLoc;
    I.Callable = variable("nct_cb", T);
    I.Args = {variable("nct_array", T.Elements[1])};
    Instruction Return;
    Return.Op = InstructionKind::Return;
    Return.Loc = InputLoc;
    if (Result.Kind != TypeKind::Void) {
      F.Locals = {{"nct_result", Result, InputLoc}};
      I.Target = variable("nct_result", Result);
      Return.Value = *I.Target;
    }
    F.Body = {label(), I, Return};
    Diagnostics D;
    EXPECT_TRUE(verifyModule(M, context(M), D));
    if (Result.Kind == TypeKind::Void) {
      F.Locals = {{"nct_result", intType(), InputLoc}};
      F.Body[1].Target = variable("nct_result");
      invalid(M, "Void indirect call");
    }
  }
}

TEST(TranslateIR, CoreV2FunctionPointersExcludeObjectOperationsAndSignatureCasts) {
  auto T = functionPointerType();
  auto P = pointerExpr(ExprKind::Null, T);
  auto M = module(true);
  M.Functions[0].Result = boolType();
  for (auto Op : {BinaryOperator::Equal, BinaryOperator::NotEqual}) {
    M.Functions[0].Body.back() = ret(binary(Op, P, P, boolType()));
    Diagnostics D;
    EXPECT_TRUE(verifyModule(M, context(M), D));
  }
  M.Functions[0].Body.back() = ret(pointerExpr(ExprKind::Cast, boolType(), {P}));
  Diagnostics D;
  EXPECT_TRUE(verifyModule(M, context(M), D));
  for (auto E : {binary(BinaryOperator::Less, P, P, boolType()),
                 binary(BinaryOperator::Subtract, P, P, intType()),
                 binary(BinaryOperator::Add, P, literal("1"), T),
                 pointerExpr(ExprKind::Dereference, intType(), {P}),
                 pointerExpr(ExprKind::Index, intType(), {P, literal("0")}),
                 pointerExpr(ExprKind::Cast, intType(), {P}),
                 pointerExpr(ExprKind::Cast, pointerType({TypeKind::Void, {}}), {P}),
                 pointerExpr(ExprKind::Cast, functionPointerType(boolType()), {P}),
                 pointerExpr(ExprKind::Cast, pointerType(functionPointerType(boolType())),
                             {pointerExpr(ExprKind::Null, pointerType(T))})}) {
    M.Functions[0].Result = E.ValueType;
    M.Functions[0].Body.back() = ret(E);
    invalid(M);
  }
}

TEST(TranslateIR, OlderProfilesRejectFunctionPointerAdditionsAtomically) {
  auto M = callbackModule();
  M.Profile = "cpp-core-v1";
  M.Target.Carriers.reset();
  invalid(M, "core v2");
}

TEST(TranslateIR, CoreV2CallbackFieldsUseIndependentPointerLayout) {
  auto Base = callbackModule();
  auto T = Base.Globals[0].ValueType;
  Base.Records = {{"nct_holder", {{"callback", T}}, InputLoc,
                   RecordLayout{{64, 64}, {0}}}};
  Diagnostics D;
  ASSERT_TRUE(verifyModule(Base, context(Base), D));
  for (const auto &Bad : {StorageLayout{32, 32}, StorageLayout{64, 32},
                          StorageLayout{128, 64}}) {
    auto M = Base;
    M.Records[0].Layout->Storage = Bad;
    invalid(M, "size or alignment disagrees");
  }
}

TEST(TranslateIR, CoreV2CallbackArrayReferencesNeedPriorCompleteRecords) {
  auto M = module(true);
  Type Item{TypeKind::Record, "nct_item"};
  auto T = functionPointerType(intType(), {pointerType(arrayType(Item, 2))});
  M.Records = {{"nct_item", {{"value", intType()}}, InputLoc,
                RecordLayout{{32, 32}, {0}}},
               {"nct_holder", {{"callback", T}}, InputLoc,
                RecordLayout{{64, 64}, {0}}}};
  EmittedSource Out;
  Diagnostics D;
  ASSERT_TRUE(emitNC(M, context(M), Out, D));
  EXPECT_NE(Out.Text.find("int (* callback)(nct_item (*)[2]);"), std::string::npos);
  EXPECT_LT(Out.Text.find("struct nct_item {"),
            Out.Text.find("struct nct_holder {"));
  std::swap(M.Records[0], M.Records[1]);
  invalid(M, "Unknown or forward/cyclic record");
}

namespace {
std::string callbackWire() {
  auto JSON = wireModule(true);
  replaceOnce(JSON, "\"result\": \"bool\"", "\"result\": \"int\"");
  replaceOnce(JSON, "\"locals\": []", R"json("locals": [
    {"name":"nct_result","type":"int","loc":{"file":"input.cpp","line":2,"column":1}}])json");
  replaceOnce(JSON, "\"kind\": \"literal\", \"type\": \"bool\", \"value\": true",
               "\"kind\": \"var\", \"type\": \"int\", \"name\": \"nct_result\"");
  auto Return = JSON.find("{\"op\": \"return\"");
  JSON.insert(Return, R"json({"op":"indirect_call","loc":{"file":"input.cpp","line":2,"column":1},
    "callable":{"kind":"function_address","type":"fnptr:0:3:int","name":"nct_target",
                "loc":{"file":"input.cpp","line":2,"column":1}},
    "args":[],"target":{"kind":"var","type":"int","name":"nct_result",
                           "loc":{"file":"input.cpp","line":2,"column":1}}},
  )json");
  JSON.insert(JSON.rfind(']'), R"json(,{
    "name":"nct_target","result":"int","internal":true,"c_export":false,"params":[],"locals":[],
    "loc":{"file":"input.cpp","line":3,"column":1},"body":[
      {"op":"label","label":"nct_entry","loc":{"file":"input.cpp","line":3,"column":1}},
      {"op":"return","loc":{"file":"input.cpp","line":3,"column":1},
       "value":{"kind":"literal","type":"int","value":"7","loc":{"file":"input.cpp","line":3,"column":1}}}
    ]})json");
  return JSON;
}
}

TEST(TranslateIR, CoreV2CallbackWireRetainsCallableAndDefinition) {
  Module M;
  Diagnostics D;
  ASSERT_TRUE(parseModule(callbackWire(), M, D));
  ASSERT_TRUE(verifyModule(M, context(M), D));
  const auto &I = M.Functions[0].Body[1];
  ASSERT_EQ(I.Op, InstructionKind::IndirectCall);
  ASSERT_TRUE(I.Callable);
  EXPECT_EQ(I.Callable->Kind, ExprKind::FunctionAddress);
  EXPECT_EQ(I.Callable->Name, M.Functions[1].Name);
  EmittedSource Output;
  ASSERT_TRUE(emitNC(M, context(M), Output, D));
  EXPECT_NE(Output.Text.find("nct_result = ((&nct_target))();"), std::string::npos);
}

TEST(TranslateIR, CoreV2CallbackWireRejectsMissingAndStrayPayloads) {
  for (const auto &Change : std::vector<std::pair<std::string, std::string>>{
       {"\"callable\":", "\"value\":"},
       {"\"callable\":", "\"callee\":\"nct_target\",\"callable\":"},
       {"\"args\":[]", "\"args\":{},\"mapping\":\"unapproved\""},
       {"\"kind\":\"function_address\",", "\"kind\":\"function_address\",\"args\":[],"},
       {"\"kind\":\"function_address\",", "\"kind\":\"function_address\",\"value\":\"0\","},
       {"\"op\":\"indirect_call\",", "\"op\":\"return\","}}) {
    auto JSON = callbackWire();
    replaceOnce(JSON, Change.first, Change.second);
    Module M;
    Diagnostics D;
    EXPECT_FALSE(parseModule(JSON, M, D)) << Change.second;
  }
}
