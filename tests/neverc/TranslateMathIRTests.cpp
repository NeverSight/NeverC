#include "NeverCTestFixture.h"
#include "ProjectIR.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

using namespace neverc::translate;
namespace {
const SourceLocation MathLoc{"math.cpp", 1, 1};
const char *AbsID = "cpp.math.fabs.f64.v1";
const char *FloorID = "cpp.math.floor.f64.v1";
Type f64() { return {TypeKind::Double, {}}; }
Type i32() { return {TypeKind::Int, {}}; }
Type u32() { return {TypeKind::UInt, {}}; }
Type boolean() { return {TypeKind::Bool, {}}; }
std::string hex(uint64_t Value, size_t Width = 16) {
  std::ostringstream S;
  S << std::hex << std::setfill('0') << std::setw(int(Width)) << Value;
  return S.str();
}
std::string identity(unsigned N) { return hex(N, 64); }
Expr floating(uint64_t Bits) {
  Expr E;
  E.ValueType = f64();
  E.Binary64Bits = Bits;
  E.Loc = MathLoc;
  return E;
}
Expr variable(std::string Name, Type T = f64()) {
  Expr E;
  E.Kind = ExprKind::Var;
  E.ValueType = T;
  E.Name = std::move(Name);
  E.Loc = MathLoc;
  return E;
}
Expr cast(Expr Value, Type T) {
  Expr E;
  E.Kind = ExprKind::Cast;
  E.ValueType = T;
  E.Args.push_back(std::move(Value));
  E.Loc = MathLoc;
  return E;
}
ProjectUnit mathUnit() {
  ProjectUnit U;
  U.TranslationUnit = "math.cpp";
  U.ConfigurationID = identity(1);
  auto &M = U.Definitions;
  M.Profile = "cpp-math-v1";
  M.Frontend = {CppFrontendName, CppFrontendVersion, "cpp-frontend-1"};
  M.Target = {"aarch64-apple-macosx15.0.0", 32, 64, true};
  M.Dependencies = {{"math.cpp", identity(2)}};
  M.FPContractID = CppMathFPContractID;
  M.SDKDistributionID = "synthetic-approved-sdk";
  M.SDKCatalogSHA256 = identity(3);
  M.SDKDependencies = {{"platform", "usr/include/math.h", identity(4)},
                       {"libcxx", "cmath", identity(5)}};
  return U;
}
VerificationContext mathContext(const ProjectUnit &U) {
  const auto &M = U.Definitions;
  VerificationContext C{M.Profile, M.Target.Triple, M.Target.IntBits,
                        M.Target.PointerBits, M.Target.LittleEndian};
  // A controlled verifier test authorization, never a production SDK/runtime
  // probe.
  C.FPContractID = CppMathFPContractID;
  C.ApprovedSDKIDs = {M.SDKDistributionID};
  C.ApprovedMappingIDs = {AbsID, FloorID};
  return C;
}
Function function(std::string Name, Expr Value,
                  std::vector<Variable> Params = {}) {
  Function F;
  F.Name = std::move(Name);
  F.Result = Value.ValueType;
  F.CExport = true;
  F.Loc = MathLoc;
  F.Params = std::move(Params);
  Instruction Label;
  Label.Op = InstructionKind::Label;
  Label.Label = "nct_entry";
  Label.Loc = MathLoc;
  Instruction Return;
  Return.Op = InstructionKind::Return;
  Return.Value = std::move(Value);
  Return.Loc = MathLoc;
  F.Body = {Label, Return};
  return F;
}
void addFunction(ProjectUnit &U, Function F, unsigned ID) {
  U.FunctionDeclarations.push_back({F.Name, identity(ID), F.Result, F.Params,
                                    F.Internal, F.CExport, false, F.Loc});
  U.ODR.push_back({EntityKind::Function, F.Name, identity(ID), "", false, F.Loc,
                   identity(5), identity(6)});
  U.Definitions.Functions.push_back(std::move(F));
}
void addMapping(ProjectUnit &U, const std::string &ID) {
  if (std::any_of(U.Definitions.Mappings.begin(), U.Definitions.Mappings.end(),
                  [&](const auto &M) { return M.ID == ID; }))
    return;
  U.Definitions.Mappings.push_back(
      {ID,
       identity(ID == AbsID ? 7 : 8),
       f64(),
       {f64()},
       {"platform", "usr/include/math.h", identity(4),
        ID == AbsID ? 423u : 466u, 1}});
}
Function mapped(std::string Name, const std::string &Mapping,
                std::optional<uint64_t> Constant = {}) {
  auto F = function(std::move(Name), variable("nct_result"));
  F.Locals = {{"nct_result", f64(), MathLoc}};
  if (!Constant)
    F.Params = {{"nct_input", f64(), MathLoc}};
  Instruction Call;
  Call.Op = InstructionKind::MappedCall;
  Call.MappingID = Mapping;
  Call.Target = variable("nct_result");
  Call.Loc = MathLoc;
  Call.Args = {Constant ? floating(*Constant) : variable("nct_input")};
  F.Body.insert(F.Body.begin() + 1, Call);
  return F;
}
void rejects(const ProjectUnit &U, llvm::StringRef Reason) {
  Diagnostics D;
  EXPECT_FALSE(verifyProjectUnit(U, mathContext(U), D));
  ASSERT_FALSE(D.empty());
  EXPECT_TRUE(std::any_of(D.begin(), D.end(), [&](const auto &E) {
    return llvm::StringRef(E.Reason).contains(Reason);
  })) << D.front().Reason;
}
bool emit(const ProjectUnit &U, EmittedProject &Out, Diagnostics &D) {
  MergedProject M;
  return mergeProjectUnits({U}, mathContext(U), M, D) &&
         emitProject(M, mathContext(U), Out, D);
}
const EmittedFile &file(const EmittedProject &P, const std::string &Name) {
  return *std::find_if(P.Files.begin(), P.Files.end(),
                       [&](const auto &F) { return F.RelativePath == Name; });
}
std::string wire(const std::string &Bits) {
  return R"json({"protocol":1,"profile":"cpp-math-v1","project_schema":1,
  "translation_unit":"math.cpp","configuration_id":")json" +
         identity(1) + R"json(",
  "frontend":{"name":"neverc-cpp-frontend","version":"20.1.8","build":"cpp-frontend-1"},
  "target":{"triple":"aarch64-apple-macosx15.0.0","int_bits":32,"pointer_bits":64,"little_endian":true},
  "dependencies":[{"path":"math.cpp","sha256":")json" +
         identity(2) + R"json("}],
  "fp_contract":"cpp.math.binary64.masked.v1","sdk_distribution_id":"synthetic-approved-sdk",
  "sdk_catalog_sha256":")json" +
         identity(3) + R"json(","sdk_dependencies":[],
  "records":[],"globals":[],"mappings":[],"diagnostics":[],"global_declarations":[],
  "function_declarations":[{"name":"sample","semantic_id":")json" +
         identity(9) + R"json(","result":"double",
    "params":[],"internal":false,"c_export":true,"inline":false,"loc":{"file":"math.cpp","line":1,"column":1}}],
  "odr":[{"kind":"function","name":"sample","semantic_id":")json" +
         identity(9) + R"json(","owner_tu":"","inline":false,
    "origin":{"file":"math.cpp","line":1,"column":1},"tokens_sha256":")json" +
         identity(5) + R"json(","bindings_sha256":")json" + identity(6) +
         R"json("}],
  "functions":[{"name":"sample","result":"double","params":[],"locals":[],"internal":false,"c_export":true,
    "loc":{"file":"math.cpp","line":1,"column":1},"body":[
      {"op":"label","label":"nct_entry","loc":{"file":"math.cpp","line":1,"column":1}},
      {"op":"return","loc":{"file":"math.cpp","line":1,"column":1},"value":{"kind":"literal","type":"double","bits":")json" +
         Bits + R"json(","loc":{"file":"math.cpp","line":1,"column":1}}}
    ]}]})json";
}
} // namespace

TEST(TranslateMathIR, ParsesExactBinary64BitsAndRejectsNoncanonicalWire) {
  for (uint64_t Bits :
       {UINT64_C(0), UINT64_C(0x8000000000000000), UINT64_C(1),
        UINT64_C(0x7ff0000000000000), UINT64_C(0x7ff8000000000123),
        UINT64_C(0xfff0000000000123)}) {
    ProjectUnit U;
    Diagnostics D;
    ASSERT_TRUE(parseProjectUnit(wire(hex(Bits)), U, D));
    ASSERT_TRUE(verifyProjectUnit(U, mathContext(U), D));
    EXPECT_EQ(U.Definitions.Functions.front().Body.back().Value->Binary64Bits,
              Bits);
  }
  for (const auto &Bits : {"0", "0x00000000000000", "FFFFFFFFFFFFFFFF",
                           "7ff800000000000g", "00000000000000000"}) {
    ProjectUnit U;
    U.TranslationUnit = "unchanged";
    Diagnostics D;
    EXPECT_FALSE(parseProjectUnit(wire(Bits), U, D));
    EXPECT_EQ(U.TranslationUnit, "unchanged");
  }
}

TEST(TranslateMathIR, RequiresIndependentDriverAuthorizations) {
  auto U = mathUnit();
  addMapping(U, AbsID);
  addFunction(U, mapped("sample", AbsID), 9);
  for (unsigned Missing = 0; Missing < 3; ++Missing) {
    auto C = mathContext(U);
    if (Missing == 0)
      C.FPContractID.clear();
    if (Missing == 1)
      C.ApprovedSDKIDs.clear();
    if (Missing == 2)
      C.ApprovedMappingIDs.clear();
    Diagnostics D;
    EXPECT_FALSE(verifyProjectUnit(U, C, D));
    EXPECT_FALSE(D.empty());
  }
  auto Bad = U;
  Bad.Definitions.FPContractID = "helper-self-approved";
  rejects(Bad, "contract");
  Bad = U;
  Bad.Definitions.Target.Triple = "x86_64-unknown-linux-gnu";
  rejects(Bad, "macOS");
  for (const auto &Profile : {"cpp-core-v1", "cpp-project-v1"}) {
    Bad = U;
    Bad.Definitions.Profile = Profile;
    Diagnostics D;
    EXPECT_FALSE(verifyProjectUnit(Bad, mathContext(Bad), D));
    auto JSON = wire("0000000000000000");
    auto Pos = JSON.find("cpp-math-v1");
    JSON.replace(Pos, 11, Profile);
    ProjectUnit Parsed;
    EXPECT_FALSE(parseProjectUnit(JSON, Parsed, D));
  }
}

TEST(TranslateMathIR, ValidatesSDKOriginsMappingSignaturesAndOperationIDs) {
  auto U = mathUnit();
  addMapping(U, AbsID);
  addFunction(U, mapped("sample", AbsID), 9);
  Diagnostics D;
  ASSERT_TRUE(verifyProjectUnit(U, mathContext(U), D));
  auto Bad = U;
  Bad.Definitions.Mappings.front().Origin.SHA256 = identity(99);
  rejects(Bad, "origin");
  Bad = U;
  Bad.Definitions.Mappings.front().Parameters.front() = i32();
  rejects(Bad, "signature");
  Bad = U;
  Bad.Definitions.Mappings.front().ID = "cpp.math.sin.f64.v1";
  rejects(Bad, "approved operation");
  Bad = U;
  Bad.Definitions.SDKDependencies.front().Path = "../math.h";
  rejects(Bad, "SDK dependency");
  Bad = U;
  Bad.Definitions.Mappings.push_back(Bad.Definitions.Mappings.front());
  rejects(Bad, "Mapping evidence");
  Bad = U;
  Bad.Definitions.Functions.front().Body[1].MappingID = FloorID;
  rejects(Bad, "Mapped call");
  Bad = U;
  Bad.Definitions.Functions.front().Body[1].Args.front().ValueType = i32();
  rejects(Bad, "type mismatch");
  Bad = U;
  Bad.Definitions.Functions.front().Body.erase(
      Bad.Definitions.Functions.front().Body.begin() + 1);
  rejects(Bad, "must correspond");
}

TEST(TranslateMathIR,
     AllowsExplicitConversionsUnaryAndComparisonsButNoFloatingArithmetic) {
  for (Type T : {i32(), u32(), boolean(), f64()}) {
    auto U = mathUnit();
    addFunction(U,
                function("sample", cast(variable("nct_value", T), f64()),
                         {{"nct_value", T, MathLoc}}),
                9);
    Diagnostics D;
    EXPECT_TRUE(verifyProjectUnit(U, mathContext(U), D));
    U = mathUnit();
    addFunction(U,
                function("sample", cast(variable("nct_value"), T),
                         {{"nct_value", f64(), MathLoc}}),
                9);
    EXPECT_TRUE(verifyProjectUnit(U, mathContext(U), D));
  }
  for (UnaryOperator Op : {UnaryOperator::Plus, UnaryOperator::Minus}) {
    Expr E;
    E.Kind = ExprKind::Unary;
    E.UnaryOp = Op;
    E.ValueType = f64();
    E.Loc = MathLoc;
    E.Args = {floating(0)};
    auto U = mathUnit();
    addFunction(U, function("sample", E), 9);
    Diagnostics D;
    EXPECT_TRUE(verifyProjectUnit(U, mathContext(U), D));
  }
  for (BinaryOperator Op :
       {BinaryOperator::Equal, BinaryOperator::NotEqual, BinaryOperator::Less,
        BinaryOperator::LessEqual, BinaryOperator::Greater,
        BinaryOperator::GreaterEqual, BinaryOperator::Add,
        BinaryOperator::Subtract, BinaryOperator::Multiply,
        BinaryOperator::Divide, BinaryOperator::Remainder,
        BinaryOperator::ShiftLeft, BinaryOperator::BitAnd}) {
    const bool Compare = Op >= BinaryOperator::Equal;
    Expr E;
    E.Kind = ExprKind::Binary;
    E.BinaryOp = Op;
    E.ValueType = Compare ? boolean() : f64();
    E.Loc = MathLoc;
    E.Args = {floating(0), floating(0x3ff0000000000000)};
    auto U = mathUnit();
    addFunction(U, function("sample", E), 9);
    Diagnostics D;
    EXPECT_EQ(verifyProjectUnit(U, mathContext(U), D), Compare);
  }
  auto U = mathUnit();
  U.Definitions.FPContractID.clear();
  U.Definitions.SDKDistributionID.clear();
  U.Definitions.SDKCatalogSHA256.clear();
  U.Definitions.SDKDependencies.clear();
  U.Definitions.Profile = "cpp-project-v1";
  addFunction(U, function("sample", floating(0)), 9);
  rejects(U, "Double requires");
}

TEST(TranslateMathIR,
     EmitsConsumerOwnedCallsAndMergesCompatibleMappingEvidence) {
  auto A = mathUnit();
  addMapping(A, AbsID);
  addMapping(A, FloorID);
  addFunction(A, mapped("sample_abs", AbsID), 9);
  addFunction(A, mapped("sample_floor", FloorID), 10);
  EmittedProject E;
  Diagnostics D;
  ASSERT_TRUE(emit(A, E, D));
  const auto &Text = file(E, "translated.nc").Text;
  EXPECT_NE(Text.find("#include <neverc/std/math.h>"), std::string::npos);
  EXPECT_NE(Text.find("neverc_math_abs(nct_input)"), std::string::npos);
  EXPECT_NE(Text.find("neverc_math_floor(nct_input)"), std::string::npos);
  EXPECT_EQ(Text.find("cpp.math."), std::string::npos);
  EXPECT_NE(file(E, "translated.h").Text.find("__DBL_MANT_DIG__ == 53"),
            std::string::npos);
  auto B = mathUnit();
  B.TranslationUnit = "other.cpp";
  B.Definitions.Dependencies.push_back({B.TranslationUnit, identity(11)});
  addMapping(B, AbsID);
  addFunction(B, mapped("other_abs", AbsID), 12);
  MergedProject P;
  ASSERT_TRUE(mergeProjectUnits({B, A}, mathContext(A), P, D));
  EXPECT_EQ(P.Definitions.Mappings.size(), 2u);
  EXPECT_EQ(P.Definitions.SDKDependencies.size(), 2u);
  B.Definitions.Mappings.front().DeclarationID = identity(99);
  EXPECT_FALSE(mergeProjectUnits({A, B}, mathContext(A), P, D));
}

TEST(TranslateMathIR,
     RejectsSignalingNaNFloorConstantsIncludingSnapshotsAndCrossUnitAliases) {
  auto U = mathUnit();
  addMapping(U, FloorID);
  addFunction(U, mapped("sample_floor", FloorID, UINT64_C(0x7ff0000000000123)),
              20);
  rejects(U, "signaling-NaN literals");
  U = mathUnit();
  addMapping(U, FloorID);
  auto F = mapped("sample_floor", FloorID);
  F.Locals.push_back({"nct_snapshot", f64(), MathLoc});
  Instruction Assign;
  Assign.Op = InstructionKind::Assign;
  Assign.Loc = MathLoc;
  Assign.Target = variable("nct_snapshot");
  Assign.Value = floating(UINT64_C(0xfff0000000000123));
  F.Body[1].Args[0] = variable("nct_snapshot");
  F.Body.insert(F.Body.begin() + 1, Assign);
  addFunction(U, F, 20);
  rejects(U, "signaling-NaN literals");
  auto A = mathUnit(), B = mathUnit();
  addMapping(A, FloorID);
  addFunction(A, mapped("sample_floor", FloorID), 20);
  B.TranslationUnit = "other.cpp";
  B.Definitions.Dependencies.push_back({"other.cpp", identity(70)});
  addFunction(
      B, function("constant_nan", floating(UINT64_C(0x7ff0000000000123))), 21);
  Diagnostics D;
  ASSERT_TRUE(verifyProjectUnit(A, mathContext(A), D));
  ASSERT_TRUE(verifyProjectUnit(B, mathContext(B), D));
  MergedProject P;
  EXPECT_FALSE(mergeProjectUnits({A, B}, mathContext(A), P, D));
  EXPECT_TRUE(
      llvm::StringRef(D.back().Reason).contains("signaling-NaN literals"));
}

class TranslateMathEmissionTest : public NeverCTest {
protected:
  void compareLines(const std::string &Generated, const std::string &Expected) {
    std::istringstream A(Generated), B(Expected);
    std::string ActualLine, ExpectedLine;
    size_t N = 0, Mismatches = 0;
    while (std::getline(A, ActualLine) && std::getline(B, ExpectedLine)) {
      ++N;
      if (ActualLine != ExpectedLine && ++Mismatches <= 8)
        ADD_FAILURE() << "Observation " << N << ": generated " << ActualLine
                      << "; reference " << ExpectedLine;
    }
    EXPECT_EQ(std::count(Generated.begin(), Generated.end(), '\n'),
              std::count(Expected.begin(), Expected.end(), '\n'));
    EXPECT_EQ(Mismatches, 0u)
        << "Observation keys are rounding mode, operation, and input index.";
  }
  void native(ProjectUnit &U) {
    U.Definitions.Target.Triple = hostTriple();
    U.Definitions.Target.PointerBits = sizeof(void *) * 8;
  }
  void write(const EmittedProject &E) {
    for (const auto &F : E.Files)
      writeFile(tmpFile(F.RelativePath), F.Text);
  }
  CmdResult compile(const fs::path &Source, const fs::path &Output,
                    const std::string &Opt,
                    std::vector<std::string> Extra = {}) {
    std::vector<std::string> Args{Source.string(), Opt, "-o", Output.string()};
    for (const auto &Flags : {archFlags(), sysrootFlags(), linkFlags(), Extra})
      Args.insert(Args.end(), Flags.begin(), Flags.end());
    return ncc(Args);
  }
};

TEST_F(TranslateMathEmissionTest,
       Binary64GlobalConstantsPreserveEveryBitAtO0AndO2) {
  if (!isDarwin())
    GTEST_SKIP() << "initial math target contract is macOS";
  auto U = mathUnit();
  native(U);
  std::vector<uint64_t> Bits{0,
                             UINT64_C(0x8000000000000000),
                             1,
                             UINT64_C(0x8000000000000001),
                             UINT64_C(0x000fffffffffffff),
                             UINT64_C(0x0010000000000000),
                             UINT64_C(0x7fefffffffffffff),
                             UINT64_C(0xffefffffffffffff),
                             UINT64_C(0x7ff0000000000000),
                             UINT64_C(0xfff0000000000000),
                             UINT64_C(0x7ff8000000000000),
                             UINT64_C(0xfff8000000000123),
                             UINT64_C(0x7ff0000000000001),
                             UINT64_C(0xfff7ffffffffffff)};
  uint64_t Seed = 0x51a79b27;
  for (unsigned I = 0; I < 256; ++I) {
    Seed ^= Seed << 13;
    Seed ^= Seed >> 7;
    Seed ^= Seed << 17;
    Bits.push_back(Seed);
  }
  std::string Checks;
  for (size_t I = 0; I < Bits.size(); ++I) {
    std::string Name = "nct_constant_" + std::to_string(I);
    U.Definitions.Globals.push_back({Name, f64(), floating(Bits[I]), MathLoc});
    U.GlobalDeclarations.push_back(
        {Name, identity(unsigned(I + 100)), f64(), true, MathLoc});
    U.ODR.push_back({EntityKind::Global, Name, identity(unsigned(I + 100)),
                     "math.cpp", false, MathLoc, identity(5), identity(6)});
    Checks += "  if (nct_bits(" + Name + ") != 0x" + hex(Bits[I]) +
              "ULL) return 1;\n";
  }
  EmittedProject E;
  Diagnostics D;
  ASSERT_TRUE(emit(U, E, D)) << (D.empty() ? "" : D.back().Reason);
  write(E);
  auto Source = file(E, "translated.nc").Text;
  Source +=
      "static unsigned long long nct_bits(double x) { union {double d; "
      "unsigned long long u;} v = {.d=x}; return v.u; }\nint main(void) {\n" +
      Checks + "return 0; }\n";
  writeFile(tmpFile("translated.nc"), Source);
  for (const auto &Opt : {"-O0", "-O2"}) {
    auto Exe = tmpFile(std::string("math_constants") + Opt);
    auto Compile = compile(tmpFile("translated.nc"), Exe, Opt);
    ASSERT_TRUE(Compile.ok()) << Compile.err;
    auto Run = exec(Exe.string(), {});
    EXPECT_TRUE(Run.ok()) << Run.exitCode << Run.err;
  }
}

TEST_F(TranslateMathEmissionTest,
       MappedCallsMatchReferenceBitsErrnoAndFlagsAtO0AndO2) {
  const char *Reference = std::getenv("NEVERC_CPP_REFERENCE_COMPILER");
  if (!isDarwin() || !Reference || !*Reference)
    GTEST_SKIP() << "requires macOS and NEVERC_CPP_REFERENCE_COMPILER";
  auto U = mathUnit();
  native(U);
  addMapping(U, AbsID);
  addMapping(U, FloorID);
  addFunction(U, mapped("translated_abs", AbsID), 20);
  addFunction(U, mapped("translated_floor", FloorID), 21);
  std::string ReferenceSource =
      "#include <cmath>\nextern \"C\" double translated_abs(double x) { return "
      "std::fabs(x); }\nextern \"C\" double translated_floor(double x) { "
      "return std::floor(x); }\n";
  const std::vector<std::pair<uint64_t, std::string>> Constants{
      {0, "0x0p+0"},
      {UINT64_C(0x8000000000000000), "-0x0p+0"},
      {1, "0x0.0000000000001p-1022"},
      {UINT64_C(0x8000000000000001), "-0x0.0000000000001p-1022"},
      {UINT64_C(0x7ff0000000000000), "__builtin_huge_val()"},
      {UINT64_C(0xfff0000000000000), "-__builtin_huge_val()"},
      {UINT64_C(0x7ff8000000000123), "__builtin_nan(\"0x123\")"},
      {UINT64_C(0xfff8000000000123), "-__builtin_nan(\"0x123\")"}};
  std::string ConstantPrototypes, ConstantNames;
  unsigned NextID = 30;
  for (size_t I = 0; I < Constants.size(); ++I)
    for (bool Abs : {true, false}) {
      std::string Name =
          std::string(Abs ? "constant_abs_" : "constant_floor_") +
          std::to_string(I);
      addFunction(U, mapped(Name, Abs ? AbsID : FloorID, Constants[I].first),
                  NextID++);
      ReferenceSource += "extern \"C\" double " + Name +
                         "() { return std::" + (Abs ? "fabs" : "floor") + "(" +
                         Constants[I].second + "); }\n";
      ConstantPrototypes += "extern \"C\" double " + Name + "();\n";
      if (!ConstantNames.empty())
        ConstantNames += ",";
      ConstantNames += Name;
    }
  EmittedProject E;
  Diagnostics D;
  ASSERT_TRUE(emit(U, E, D)) << (D.empty() ? "" : D.back().Reason);
  write(E);
  writeFile(tmpFile("reference.cpp"), ReferenceSource);
  std::string Harness = R"cpp(#include <cstdio>
#include <cerrno>
#include <cfenv>
#include <cstring>
extern "C" double translated_abs(double);
extern "C" double translated_floor(double);
)cpp" + ConstantPrototypes +
                        R"cpp(
static unsigned long long bits(double x) { unsigned long long u; std::memcpy(&u,&x,8); return u; }
static double value(unsigned long long u) { double x; std::memcpy(&x,&u,8); return x; }
static void reset() { std::feclearexcept(FE_ALL_EXCEPT); std::feraiseexcept(FE_DIVBYZERO); errno=123; }
static void print(unsigned mode, unsigned op, unsigned n, double result) {
  int error=errno, flags=std::fetestexcept(FE_ALL_EXCEPT);
  std::printf("%u %u %u %016llx %d %d\n",mode,op,n,bits(result),error,flags);
}
int main() {
  double (*operations[])(double)={translated_abs,translated_floor};
  double (*constants[])()={)cpp" +
                        ConstantNames + R"cpp(};
  unsigned long long inputs[]={0ULL,0x8000000000000000ULL,1ULL,0x8000000000000001ULL,
    0x000fffffffffffffULL,0x0010000000000000ULL,0x3fefffffffffffffULL,0x3ff0000000000000ULL,
    0x3ff0000000000001ULL,0xbfefffffffffffffULL,0xbff0000000000001ULL,0x7fefffffffffffffULL,
    0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000123ULL,0xfff8000000000123ULL,
    0x7ff0000000000123ULL,0xfff0000000000123ULL};
  int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
  for(unsigned mode=0;mode<4;++mode) {
    if(std::fesetround(modes[mode])) return 1;
    for(unsigned op=0;op<2;++op) {
      unsigned n=0;
      for(auto input:inputs) { double x=value(input); reset(); double result=operations[op](x); print(mode,op,n++,result); }
      unsigned long long seed=0x734129b5ULL;
      for(unsigned j=0;j<128;++j) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;
        double x=value(seed);reset();double result=operations[op](x);print(mode,op,n++,result); }
    }
    for(unsigned n=0;n<sizeof(constants)/sizeof(constants[0]);++n) {
      reset();double result=constants[n]();print(mode,2,n,result);
    }
  }
  return 0;
}
)cpp";
  writeFile(tmpFile("harness.cpp"), Harness);
  for (const auto &Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto GeneratedObject = tmpFile(std::string("generated") + Opt + ".o");
    auto GeneratedCompile =
        compile(tmpFile("translated.nc"), GeneratedObject, Opt, {"-c"});
    ASSERT_TRUE(GeneratedCompile.ok()) << GeneratedCompile.err;
    auto ReferenceObject = tmpFile(std::string("reference") + Opt + ".o");
    auto ReferenceCompile =
        exec(Reference,
             {"-std=c++17", "--target=" + hostTriple(), Opt,
              "-ffp-contract=off", "-c", tmpFile("reference.cpp").string(),
              "-o", ReferenceObject.string()});
    ASSERT_TRUE(ReferenceCompile.ok()) << ReferenceCompile.err;
    auto GeneratedExe = tmpFile(std::string("generated") + Opt);
    auto ReferenceExe = tmpFile(std::string("reference") + Opt);
    for (const auto &Pair : {std::make_pair(GeneratedObject, GeneratedExe),
                             std::make_pair(ReferenceObject, ReferenceExe)}) {
      auto Link =
          exec(Reference, {"-std=c++17", "--target=" + hostTriple(), Opt,
                           tmpFile("harness.cpp").string(), Pair.first.string(),
                           "-o", Pair.second.string()});
      ASSERT_TRUE(Link.ok()) << Link.err;
    }
    auto Generated = exec(GeneratedExe.string(), {}),
         Expected = exec(ReferenceExe.string(), {});
    ASSERT_TRUE(Generated.ok()) << Generated.err;
    ASSERT_TRUE(Expected.ok()) << Expected.err;
    EXPECT_EQ(std::count(Expected.out.begin(), Expected.out.end(), '\n'), 1232);
    compareLines(Generated.out, Expected.out);
  }
}

TEST_F(TranslateMathEmissionTest,
       ConversionsUnaryAndComparisonsMatchReferenceEnvironment) {
  const char *Reference = std::getenv("NEVERC_CPP_REFERENCE_COMPILER");
  if (!isDarwin() || !Reference || !*Reference)
    GTEST_SKIP() << "requires macOS and NEVERC_CPP_REFERENCE_COMPILER";
  auto U = mathUnit();
  native(U);
  unsigned NextID = 50;
  std::string ReferenceSource;
  auto CastFunction = [&](const std::string &Name, Type From, Type To,
                          const std::string &FromC, const std::string &ToC) {
    addFunction(U,
                function(Name, cast(variable("nct_input", From), To),
                         {{"nct_input", From, MathLoc}}),
                NextID++);
    ReferenceSource += "extern \"C\" " + ToC + " " + Name + "(" + FromC +
                       " x) { return static_cast<" + ToC + ">(x); }\n";
  };
  CastFunction("to_i", f64(), i32(), "double", "int");
  CastFunction("to_u", f64(), u32(), "double", "unsigned int");
  CastFunction("to_b", f64(), boolean(), "double", "bool");
  CastFunction("from_i", i32(), f64(), "int", "double");
  CastFunction("from_u", u32(), f64(), "unsigned int", "double");
  CastFunction("from_b", boolean(), f64(), "bool", "double");
  for (const auto &Entry : {std::make_pair("positive", UnaryOperator::Plus),
                            std::make_pair("negative", UnaryOperator::Minus)}) {
    Expr E;
    E.Kind = ExprKind::Unary;
    E.UnaryOp = Entry.second;
    E.ValueType = f64();
    E.Loc = MathLoc;
    E.Args = {variable("nct_input")};
    addFunction(U, function(Entry.first, E, {{"nct_input", f64(), MathLoc}}),
                NextID++);
    ReferenceSource += "extern \"C\" double " + std::string(Entry.first) +
                       "(double x) { return " +
                       (Entry.second == UnaryOperator::Plus ? "+" : "-") +
                       "x; }\n";
  }
  const std::vector<std::pair<const char *, BinaryOperator>> Comparisons{
      {"==", BinaryOperator::Equal},  {"!=", BinaryOperator::NotEqual},
      {"<", BinaryOperator::Less},    {"<=", BinaryOperator::LessEqual},
      {">", BinaryOperator::Greater}, {">=", BinaryOperator::GreaterEqual}};
  std::string Prototypes, Names;
  for (size_t I = 0; I < Comparisons.size(); ++I) {
    std::string Name = "compare_" + std::to_string(I);
    Expr E;
    E.Kind = ExprKind::Binary;
    E.BinaryOp = Comparisons[I].second;
    E.ValueType = boolean();
    E.Loc = MathLoc;
    E.Args = {variable("nct_a"), variable("nct_b")};
    addFunction(
        U,
        function(Name, E,
                 {{"nct_a", f64(), MathLoc}, {"nct_b", f64(), MathLoc}}),
        NextID++);
    ReferenceSource += "extern \"C\" bool " + Name +
                       "(double a,double b) { return a " +
                       Comparisons[I].first + " b; }\n";
    Prototypes += "extern \"C\" bool " + Name + "(double,double);\n";
    if (!Names.empty())
      Names += ",";
    Names += Name;
  }
  EmittedProject E;
  Diagnostics D;
  ASSERT_TRUE(emit(U, E, D));
  write(E);
  writeFile(tmpFile("reference.cpp"), ReferenceSource);
  std::string Harness = R"cpp(#include <cstdio>
#include <cerrno>
#include <cfenv>
#include <cstring>
extern "C" int to_i(double); extern "C" unsigned int to_u(double); extern "C" bool to_b(double);
extern "C" double from_i(int); extern "C" double from_u(unsigned int); extern "C" double from_b(bool);
extern "C" double positive(double); extern "C" double negative(double);
)cpp" + Prototypes + R"cpp(
static unsigned long long bits(double x) { unsigned long long u; std::memcpy(&u,&x,8); return u; }
static double value(unsigned long long u) { double x; std::memcpy(&x,&u,8); return x; }
static void reset() { std::feclearexcept(FE_ALL_EXCEPT); std::feraiseexcept(FE_DIVBYZERO); errno=123; }
static void print(unsigned mode,unsigned op,unsigned n,unsigned long long result) {
  int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
  std::printf("%u %u %u %016llx %d %d\n",mode,op,n,result,error,flags);
}
int main() {
  unsigned long long inputs[]={0,0x8000000000000000ULL,1,0x8000000000000001ULL,0x3ff0000000000000ULL,
    0xbff0000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,
    0x7ff8000000000123ULL,0xfff8000000000123ULL,0x7ff0000000000123ULL,0xfff0000000000123ULL};
  bool (*comparisons[])(double,double)={)cpp" +
                        Names + R"cpp(};
  int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
  for(unsigned mode=0;mode<4;++mode) {
    if(std::fesetround(modes[mode])) return 1;
    for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i) {
      double x=value(inputs[i]);
      reset();auto b=to_b(x);print(mode,0,i,b);
      reset();auto p=positive(x);print(mode,1,i,bits(p));
      reset();auto n=negative(x);print(mode,2,i,bits(n));
      for(unsigned j=0;j<6;++j) for(unsigned k=0;k<sizeof(inputs)/sizeof(inputs[0]);++k) {
        double y=value(inputs[k]);reset();auto r=comparisons[j](x,y);print(mode,3+j,i*100+k,r);
      }
    }
    double signed_inputs[]={-2147483648.0,-2147483647.75,-3.75,-0.75,0.75,3.75,2147483647.75};
    for(unsigned i=0;i<sizeof(signed_inputs)/sizeof(signed_inputs[0]);++i) {
      reset();int r=to_i(signed_inputs[i]);print(mode,10,i,(unsigned int)r);
    }
    double unsigned_inputs[]={-0.75,0,0.75,3.75,2147483648.75,4294967295.75};
    for(unsigned i=0;i<sizeof(unsigned_inputs)/sizeof(unsigned_inputs[0]);++i) {
      reset();auto r=to_u(unsigned_inputs[i]);print(mode,11,i,r);
    }
    int signed_values[]={(-2147483647-1),-1,0,1,2147483647};
    for(unsigned i=0;i<5;++i) {reset();double r=from_i(signed_values[i]);print(mode,12,i,bits(r));}
    unsigned unsigned_values[]={0,1,2147483648u,4294967295u};
    for(unsigned i=0;i<4;++i) {reset();double r=from_u(unsigned_values[i]);print(mode,13,i,bits(r));}
    for(unsigned i=0;i<2;++i) {reset();double r=from_b(i!=0);print(mode,14,i,bits(r));}
  }
  return 0;
}
)cpp";
  writeFile(tmpFile("harness.cpp"), Harness);
  for (const auto &Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto GeneratedObject = tmpFile(std::string("generated") + Opt + ".o");
    auto GeneratedCompile =
        compile(tmpFile("translated.nc"), GeneratedObject, Opt, {"-c"});
    ASSERT_TRUE(GeneratedCompile.ok()) << GeneratedCompile.err;
    auto ReferenceObject = tmpFile(std::string("reference") + Opt + ".o");
    auto ReferenceCompile =
        exec(Reference,
             {"-std=c++17", "--target=" + hostTriple(), Opt,
              "-ffp-contract=off", "-c", tmpFile("reference.cpp").string(),
              "-o", ReferenceObject.string()});
    ASSERT_TRUE(ReferenceCompile.ok()) << ReferenceCompile.err;
    auto GeneratedExe = tmpFile(std::string("generated") + Opt),
         ReferenceExe = tmpFile(std::string("reference") + Opt);
    for (const auto &Pair : {std::make_pair(GeneratedObject, GeneratedExe),
                             std::make_pair(ReferenceObject, ReferenceExe)}) {
      auto Link =
          exec(Reference, {"-std=c++17", "--target=" + hostTriple(), Opt,
                           tmpFile("harness.cpp").string(), Pair.first.string(),
                           "-o", Pair.second.string()});
      ASSERT_TRUE(Link.ok()) << Link.err;
    }
    auto Generated = exec(GeneratedExe.string(), {}),
         Expected = exec(ReferenceExe.string(), {});
    ASSERT_TRUE(Generated.ok());
    ASSERT_TRUE(Expected.ok());
    EXPECT_EQ(std::count(Expected.out.begin(), Expected.out.end(), '\n'), 3696);
    compareLines(Generated.out, Expected.out);
  }
}
