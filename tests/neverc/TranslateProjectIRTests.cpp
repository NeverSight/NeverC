#include "NeverCTestFixture.h"
#include "ProjectIR.h"
#include <algorithm>
#include <set>

using namespace neverc::translate;
namespace {
Type projectInt() { return {TypeKind::Int, {}}; }
SourceLocation source(const std::string &Path) { return {Path, 1, 1}; }
std::string hash(char C) { return std::string(64, C); }
Expr integer(int Value, const SourceLocation &L) {
  Expr E;
  E.ValueType = projectInt();
  E.Integer = std::to_string(Value);
  E.Loc = L;
  return E;
}
Expr storage(const std::string &Name, const SourceLocation &L) {
  Expr E;
  E.Kind = ExprKind::Var;
  E.ValueType = projectInt();
  E.Name = Name;
  E.Loc = L;
  return E;
}
Function function(const std::string &Name, const SourceLocation &L,
                  int Value = 42, bool Internal = false) {
  Function F;
  F.Name = Name;
  F.Result = projectInt();
  F.Loc = L;
  F.Internal = Internal;
  F.CExport = !Internal && Name != "main" && Name.find("nct_") != 0;
  Instruction Label;
  Label.Op = InstructionKind::Label;
  Label.Loc = L;
  Label.Label = "nct_entry";
  Instruction Return;
  Return.Op = InstructionKind::Return;
  Return.Loc = L;
  Return.Value = integer(Value, L);
  F.Body = {Label, Return};
  return F;
}
FunctionDeclaration declaration(const Function &F, char ID,
                                bool Inline = false) {
  FunctionDeclaration D;
  D.Name = F.Name;
  D.SemanticID = hash(ID);
  D.Result = F.Result;
  D.Params = F.Params;
  D.Internal = F.Internal;
  D.CExport = F.CExport;
  D.Inline = Inline;
  D.Loc = F.Loc;
  return D;
}
ProjectUnit unit(const std::string &TU) {
  ProjectUnit U;
  U.TranslationUnit = TU;
  U.ConfigurationID = hash('c');
  auto &M = U.Definitions;
  M.Profile = "cpp-project-v1";
  M.Frontend = {CppFrontendName, CppFrontendVersion, "cpp-frontend-1"};
  M.Target = {"x86_64-unknown-linux-gnu", 32, 64, true};
  M.Dependencies = {{TU, hash('a')}, {"inc/shared.h", hash('b')}};
  return U;
}
VerificationContext projectContext(const ProjectUnit &U) {
  const auto &M = U.Definitions;
  return {M.Profile, M.Target.Triple, M.Target.IntBits, M.Target.PointerBits,
          M.Target.LittleEndian};
}
void addFunction(ProjectUnit &U, Function F, char ID, bool Inline = false) {
  U.FunctionDeclarations.push_back(declaration(F, ID, Inline));
  U.ODR.push_back({EntityKind::Function, F.Name, hash(ID),
                   F.Internal ? U.TranslationUnit : "", Inline, F.Loc,
                   hash('d'), hash('e')});
  U.Definitions.Functions.push_back(std::move(F));
}
void addRecord(ProjectUnit &U, const std::string &Name, char ID,
               bool Internal = false) {
  Record R{Name, {{"nct_field", projectInt()}}, source("inc/shared.h")};
  U.ODR.push_back({EntityKind::Record, Name, hash(ID),
                   Internal ? U.TranslationUnit : "", false, R.Loc, hash('d'),
                   hash('e')});
  U.Definitions.Records.push_back(std::move(R));
}
void addGlobal(ProjectUnit &U, const std::string &Name, char ID,
               bool Internal) {
  auto L = source(U.TranslationUnit);
  U.GlobalDeclarations.push_back({Name, hash(ID), projectInt(), Internal, L});
  U.ODR.push_back({EntityKind::Global, Name, hash(ID),
                   Internal ? U.TranslationUnit : "", false, L, hash('d'),
                   hash('e')});
  U.Definitions.Globals.push_back({Name, projectInt(), integer(42, L), L});
}
bool merge(const std::vector<ProjectUnit> &Units, MergedProject &P,
           Diagnostics &D) {
  return mergeProjectUnits(Units, projectContext(Units.front()), P, D);
}
void rejects(const std::vector<ProjectUnit> &Units, llvm::StringRef Reason) {
  MergedProject P;
  P.Definitions.Profile = "unchanged";
  Diagnostics D;
  EXPECT_FALSE(merge(Units, P, D));
  ASSERT_FALSE(D.empty());
  EXPECT_EQ(P.Definitions.Profile, "unchanged");
  EXPECT_TRUE(std::any_of(D.begin(), D.end(), [&](const auto &E) {
    return llvm::StringRef(E.Reason).contains(Reason);
  })) << D.front().Reason;
}
const EmittedFile &file(const EmittedProject &P, const std::string &Name) {
  return *std::find_if(P.Files.begin(), P.Files.end(),
                       [&](const auto &F) { return F.RelativePath == Name; });
}
std::vector<ProjectUnit> crossUnitCalls() {
  ProjectUnit A = unit("src/a.cpp"), B = unit("src/b.cpp");
  Function G = function("answer", source(B.TranslationUnit));
  addFunction(B, G, '2');
  auto GD = declaration(G, '2');
  GD.Loc = source("inc/shared.h");
  A.FunctionDeclarations.push_back(GD);
  Function F = function("main", source(A.TranslationUnit), 0);
  F.Locals.push_back({"nct_result", projectInt(), F.Loc});
  Instruction Call;
  Call.Op = InstructionKind::Call;
  Call.Loc = F.Loc;
  Call.Callee = "answer";
  Call.Target = storage("nct_result", F.Loc);
  F.Body.insert(F.Body.begin() + 1, Call);
  Expr Minus;
  Minus.Kind = ExprKind::Binary;
  Minus.BinaryOp = BinaryOperator::Subtract;
  Minus.ValueType = projectInt();
  Minus.Loc = F.Loc;
  Minus.Args = {storage("nct_result", F.Loc), integer(42, F.Loc)};
  F.Body.back().Value = Minus;
  addFunction(A, F, '1');
  return {A, B};
}
} // namespace

TEST(TranslateProjectIR, ParsesSeparateProjectEnvelopeAndKeepsCoreStrict) {
  const std::string JSON = R"json({
    "protocol":1,"profile":"cpp-project-v1","project_schema":1,
    "translation_unit":"a.cpp","configuration_id":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
    "frontend":{"name":"neverc-cpp-frontend","version":"20.1.8","build":"cpp-frontend-1"},
    "target":{"triple":"x86_64-unknown-linux-gnu","int_bits":32,"pointer_bits":64,"little_endian":true},
    "dependencies":[{"path":"a.cpp","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}],
    "records":[],"globals":[],"functions":[],"mappings":[],"diagnostics":[],
    "function_declarations":[],"global_declarations":[],"odr":[]
  })json";
  ProjectUnit U;
  Diagnostics D;
  ASSERT_TRUE(parseProjectUnit(JSON, U, D));
  EXPECT_TRUE(verifyProjectUnit(U, projectContext(U), D));
  EXPECT_FALSE(verifyModule(U.Definitions, projectContext(U), D));
  auto Bad = JSON;
  Bad.replace(Bad.find("\"project_schema\":1"), 18, "\"project_schema\":2");
  U.TranslationUnit = "unchanged";
  EXPECT_FALSE(parseProjectUnit(Bad, U, D));
  EXPECT_EQ(U.TranslationUnit, "unchanged");
}

TEST(TranslateProjectIR,
     AllowsDeclaredCrossUnitCallsAndRequiresFinalDefinitionClosure) {
  auto Units = crossUnitCalls();
  Diagnostics D;
  ASSERT_TRUE(verifyProjectUnit(Units[0], projectContext(Units[0]), D));
  MergedProject P;
  ASSERT_TRUE(merge(Units, P, D)) << (D.empty() ? "" : D.back().Reason);
  ASSERT_EQ(P.Definitions.Functions.size(), 2u);
  EXPECT_EQ(P.Definitions.Exports.size(), 2u);
  rejects({Units[0]}, "no definition");
  Units[0].FunctionDeclarations.front().Result = {TypeKind::UInt, {}};
  rejects(Units, "result storage");
}

TEST(TranslateProjectIR, RejectsStrongDuplicatesAndDeclarationConflicts) {
  auto A = unit("a.cpp"), B = unit("b.cpp");
  addFunction(A, function("api", source("inc/shared.h")), '1');
  addFunction(B, function("api", source("inc/shared.h")), '1');
  rejects({A, B}, "Duplicate strong");
  B.FunctionDeclarations.front().SemanticID = hash('2');
  B.ODR.front().SemanticID = hash('2');
  rejects({A, B}, "identity");
  B = unit("b.cpp");
  Function G = function("api", source(B.TranslationUnit));
  G.Result = {TypeKind::UInt, {}};
  G.Body.back().Value->ValueType = G.Result;
  addFunction(B, G, '1');
  rejects({A, B}, "Conflicting function declaration");
}

TEST(TranslateProjectIR,
     DeduplicatesOnlyMatchingHeaderInlineAndRecordEvidence) {
  auto A = unit("a.cpp"), B = unit("b.cpp");
  addRecord(A, "nct_record", '3');
  addRecord(B, "nct_record", '3');
  addFunction(A, function("nct_inline", source("inc/shared.h")), '1', true);
  auto F = function("nct_inline", source("inc/shared.h"));
  F.Body[0].Label = "nct_other_entry";
  addFunction(B, F, '1', true);
  MergedProject P;
  Diagnostics D;
  ASSERT_TRUE(merge({A, B}, P, D)) << (D.empty() ? "" : D.back().Reason);
  EXPECT_EQ(P.Definitions.Records.size(), 1u);
  EXPECT_EQ(P.Definitions.Functions.size(), 1u);
  ASSERT_EQ(P.Entities.size(), 2u);
  EXPECT_EQ(P.Entities.front().Origins.size(), 2u);
  auto Bad = B;
  Bad.ODR.back().TokensSHA256 = hash('f');
  rejects({A, Bad}, "conflicting tokens");
  Bad = B;
  Bad.ODR.back().BindingsSHA256 = hash('f');
  rejects({A, Bad}, "resolved bindings");
  Bad = B;
  Bad.Definitions.Functions.front().Body.back().Value->Integer = "43";
  rejects({A, Bad}, "typed body");
  Bad = B;
  Bad.Definitions.Records.front().Fields.front().ValueType = {TypeKind::UInt,
                                                              {}};
  rejects({A, Bad}, "typed body");
}

TEST(TranslateProjectIR, KeepsPrivateEntitiesDistinctAndOutOfPublicHeader) {
  auto A = unit("a.cpp"), B = unit("b.cpp");
  addFunction(A, function("nct_private_a", source("inc/shared.h"), 1, true),
              '1');
  addFunction(B, function("nct_private_b", source("inc/shared.h"), 2, true),
              '2');
  addRecord(A, "nct_record_a", '3', true);
  addRecord(B, "nct_record_b", '4', true);
  addGlobal(A, "nct_constant_a", '5', true);
  addGlobal(B, "nct_constant_b", '6', true);
  MergedProject P;
  Diagnostics D;
  ASSERT_TRUE(merge({A, B}, P, D));
  EXPECT_EQ(P.Definitions.Functions.size(), 2u);
  EXPECT_EQ(P.Definitions.Globals.size(), 2u);
  EmittedProject E;
  ASSERT_TRUE(emitProject(P, projectContext(A), E, D));
  const auto &H = file(E, "translated.h");
  const auto &S = file(E, "translated.nc");
  EXPECT_EQ(H.Text.find("nct_private"), std::string::npos);
  EXPECT_EQ(H.Text.find("nct_record"), std::string::npos);
  EXPECT_EQ(H.Text.find("nct_constant"), std::string::npos);
  EXPECT_NE(S.Text.find("static int nct_private_a"), std::string::npos);
  EXPECT_NE(S.Text.find("static const int nct_constant_b"), std::string::npos);
  B = A;
  B.TranslationUnit = "b.cpp";
  B.Definitions.Dependencies.front().Path = "b.cpp";
  for (auto &O : B.ODR)
    O.OwnerTU = "b.cpp";
  // The duplicate internal function and record still expose a missing TU salt.
  B.Definitions.Globals.clear();
  B.GlobalDeclarations.clear();
  B.ODR.pop_back();
  rejects({A, B}, "TU-private");
}

TEST(TranslateProjectIR,
     CanonicalizesInlineStorageAndOrdersPublicRecordDependencies) {
  auto A = unit("a.cpp"), B = unit("b.cpp");
  auto AddInline = [&](ProjectUnit &U, const std::string &Parameter,
                       const std::string &Local) {
    auto L = source("inc/shared.h");
    auto F = function("nct_inline", L);
    F.Params.push_back({Parameter, projectInt(), L});
    F.Locals.push_back({Local, projectInt(), L});
    Instruction Assign;
    Assign.Op = InstructionKind::Assign;
    Assign.Loc = L;
    Assign.Target = storage(Local, L);
    Assign.Value = storage(Parameter, L);
    F.Body.insert(F.Body.begin() + 1, Assign);
    F.Body.back().Value = storage(Local, L);
    addFunction(U, F, '1', true);
  };
  AddInline(A, "nct_parameter_a", "nct_local_a");
  AddInline(B, "nct_parameter_b", "nct_local_b");
  // The public parent sorts before its private dependency alphabetically, so
  // stable ordering must respect the type graph and promote the dependency.
  addRecord(A, "nct_z_private", '2', true);
  addRecord(A, "nct_a_public", '3');
  A.Definitions.Records.back().Fields.front().ValueType = {TypeKind::Record,
                                                           "nct_z_private"};
  MergedProject P;
  Diagnostics D;
  ASSERT_TRUE(merge({B, A}, P, D)) << (D.empty() ? "" : D.back().Reason);
  ASSERT_EQ(P.Definitions.Functions.size(), 1u);
  ASSERT_EQ(P.Definitions.Records.size(), 2u);
  EXPECT_EQ(P.Definitions.Records.front().ID, "nct_z_private");
  EmittedProject E;
  ASSERT_TRUE(emitProject(P, projectContext(A), E, D));
  const auto &H = file(E, "translated.h").Text;
  EXPECT_LT(H.find("typedef struct nct_z_private"),
            H.find("typedef struct nct_a_public"));
  EXPECT_EQ(file(E, "translated.nc").Text.find("typedef struct"),
            std::string::npos);
}

TEST(TranslateProjectIR,
     ChecksOwnershipHashesEvidenceAndInlineLocalDefinitions) {
  auto Units = crossUnitCalls();
  auto Bad = Units;
  Bad[1].ODR.clear();
  rejects(Bad, "Every definition");
  Bad = Units;
  Bad[1].ODR.front().OwnerTU = "src/a.cpp";
  rejects(Bad, "ODR evidence");
  Bad = Units;
  Bad[1].ODR.front().SemanticID = hash('f');
  rejects(Bad, "identity");
  Bad = Units;
  Bad[1].Definitions.Dependencies.back().SHA256 = hash('f');
  rejects(Bad, "dependency bytes differ");
  Bad = Units;
  Bad[0].FunctionDeclarations.front().Inline = true;
  rejects(Bad, "local definition");
  Bad = Units;
  Bad[1].ConfigurationID = "not-a-digest";
  rejects(Bad, "configuration identity");
  Bad = Units;
  Bad[1].Definitions.Frontend.Build = "another-build";
  rejects(Bad, "frontend build");
  rejects({Units[0], Units[0]}, "same source");
}

TEST(TranslateProjectIR, MergesExternalConstantsAndMapsBothSourceAndHeader) {
  auto A = unit("a.cpp"), B = unit("b.cpp");
  addGlobal(B, "nct_external_constant", '3', false);
  auto GD = B.GlobalDeclarations.front();
  GD.Loc = source("inc/shared.h");
  A.GlobalDeclarations.push_back(GD);
  auto F = function("api", source(A.TranslationUnit));
  F.Body.back().Value = storage(GD.Name, F.Loc);
  addFunction(A, F, '1');
  addRecord(A, "nct_shared_record", '4');
  addRecord(B, "nct_shared_record", '4');
  MergedProject P;
  Diagnostics D;
  ASSERT_TRUE(merge({A, B}, P, D));
  EmittedProject E;
  ASSERT_TRUE(emitProject(P, projectContext(A), E, D));
  EXPECT_NE(file(E, "translated.h")
                .Text.find("extern const int nct_external_constant;"),
            std::string::npos);
  EXPECT_NE(
      file(E, "translated.nc").Text.find("\nconst int nct_external_constant ="),
      std::string::npos);
  std::set<std::string> Origins;
  for (const auto &F : E.Files)
    for (const auto &Map : F.Map) {
      EXPECT_LE(Map.EndLine, std::count(F.Text.begin(), F.Text.end(), '\n'));
      Origins.insert(Map.Original.File);
    }
  EXPECT_EQ(Origins, (std::set<std::string>{"a.cpp", "b.cpp", "inc/shared.h"}));
}

TEST(TranslateProjectIR,
     EmissionIsIndependentOfSelectionOrderAndRejectsTamperedMergedData) {
  auto Units = crossUnitCalls();
  MergedProject A, B;
  Diagnostics D;
  ASSERT_TRUE(merge(Units, A, D));
  std::reverse(Units.begin(), Units.end());
  ASSERT_TRUE(merge(Units, B, D));
  EmittedProject EA, EB;
  ASSERT_TRUE(emitProject(A, projectContext(Units.front()), EA, D));
  ASSERT_TRUE(emitProject(B, projectContext(Units.front()), EB, D));
  ASSERT_EQ(EA.Files.size(), EB.Files.size());
  for (size_t I = 0; I < EA.Files.size(); ++I)
    EXPECT_EQ(EA.Files[I].Text, EB.Files[I].Text);
  A.Entities.clear();
  EXPECT_FALSE(emitProject(A, projectContext(Units.front()), EA, D));
}

class TranslateProjectEmissionTest : public NeverCTest {};
TEST_F(TranslateProjectEmissionTest,
       CompilesCombinedHeaderAndCrossUnitCallsAtO0AndO2) {
  auto Units = crossUnitCalls();
  for (auto &U : Units) {
    U.Definitions.Target.Triple = hostTriple();
    U.Definitions.Target.PointerBits = sizeof(void *) * 8;
  }
  MergedProject P;
  Diagnostics D;
  ASSERT_TRUE(merge(Units, P, D));
  EmittedProject E;
  ASSERT_TRUE(emitProject(P, projectContext(Units.front()), E, D));
  for (const auto &F : E.Files)
    writeFile(tmpFile(F.RelativePath), F.Text);
  for (const auto &Opt : {"-O0", "-O2"}) {
    auto Exe =
        tmpFile(std::string("project") + Opt + (isWindows() ? ".exe" : ""));
    std::vector<std::string> Args{tmpFile("translated.nc").string(), Opt, "-o",
                                  Exe.string()};
    for (const auto &Flags : {archFlags(), sysrootFlags(), linkFlags()})
      Args.insert(Args.end(), Flags.begin(), Flags.end());
    auto Compile = ncc(Args);
    ASSERT_TRUE(Compile.ok()) << Compile.err;
    auto Run = exec(Exe.string(), {});
    EXPECT_TRUE(Run.ok()) << Run.err << Run.exitCode;
  }
}
