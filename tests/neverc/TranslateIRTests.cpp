#include "NeverCTestFixture.h"
#include "TranslateIR.h"
#include <algorithm>

using namespace neverc::translate;

namespace {
const SourceLocation InputLoc{"input.cpp", 1, 1};
Type intType() { return {TypeKind::Int, {}}; }
Type uintType() { return {TypeKind::UInt, {}}; }
Type boolType() { return {TypeKind::Bool, {}}; }
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
Module module() {
  Module M;
  M.Profile = "cpp-core-v1";
  M.Frontend = {CppFrontendName, CppFrontendVersion, "cpp-frontend-1"};
  M.Target = {"x86_64-unknown-linux-gnu", 32, 64, true};
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
  return {M.Profile, M.Target.Triple, M.Target.IntBits, M.Target.PointerBits,
          M.Target.LittleEndian};
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
std::string wireModule() {
  return R"json({
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
