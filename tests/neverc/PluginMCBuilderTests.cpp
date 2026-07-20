#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

class MCTaskScope {
public:
  MCTaskScope()
      : Services("neverc-plugin-mc-builder-tests", LLVM_VERSION_MAJOR) {}

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

PluginTargetSnapshot::NamedRecord schema() {
  PluginTargetSnapshot::NamedRecord Schema;
  Schema.ID = {UINT64_C(0x4e43504d43534348), 1};
  Schema.TargetID = {UINT64_C(0x4e43505441524754), 1};
  Schema.CanonicalName = "test.mc-builder-schema";
  Schema.Digest =
      "2222222222222222222222222222222222222222222222222222222222222222";
  Schema.Opcodes = {{10, 100, "test.return", 0}};
  Schema.SchemaRegisters = {{20, 5, "test.link", 0}};
  Schema.Relocations = {{30, 128, "test.branch-fixup", 0}};
  Schema.Variants = {{40, 1, "test.page", 0}};
  return Schema;
}

struct BuilderContext {
  MCTaskScope Scope;
  PluginMCUnit Unit;
  PluginTargetSnapshot::NamedRecord Schema = schema();
  std::unique_ptr<MCPluginBridge> Bridge;
  NevercMCUnitHandle UnitHandle{};
  NevercMCSchemaTokenHandle SchemaToken{};

  bool initialize() {
    if (!Scope.initialize())
      return false;
    Bridge = std::make_unique<MCPluginBridge>(Scope.task(), Unit, &Schema);
    auto Handle = Bridge->unit();
    if (!Handle) {
      ADD_FAILURE() << errorText(Handle.takeError());
      return false;
    }
    UnitHandle = *Handle;
    NevercStatus Status = Bridge->api().GetSchemaToken(
        Bridge->api().Context, Scope.task().handle(), UnitHandle,
        &SchemaToken);
    if (Status.Code != NEVERC_STATUS_OK) {
      ADD_FAILURE() << "failed to acquire schema token";
      return false;
    }
    return true;
  }

  const NevercMCAPI &api() const { return Bridge->api(); }
};

NevercMCMutationHandle beginMutation(BuilderContext &State) {
  NevercMCMutationHandle Mutation{};
  EXPECT_EQ(State.api()
                .BeginMutation(State.api().Context, State.Scope.task().handle(),
                               State.UnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  return Mutation;
}

TEST(PluginMCBuilderTest, BuildsAndCopiesCompleteUnitAtomically) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercMCAPI &API = State.api();
  NevercMCMutationHandle Mutation = beginMutation(State);

  char SectionName[] = ".text";
  NevercMCSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor), NEVERC_MC_API_MAJOR,
                              NEVERC_MC_API_MINOR, 0};
  SectionDescriptor.Name = {SectionName, 5};
  SectionDescriptor.Alignment = 16;
  SectionDescriptor.Flags =
      NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
  NevercMCSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                              Mutation, &SectionDescriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);

  char SymbolName[] = "_start";
  NevercMCSymbolDescriptor SymbolDescriptor{};
  SymbolDescriptor.Header = {sizeof(SymbolDescriptor), NEVERC_MC_API_MAJOR,
                             NEVERC_MC_API_MINOR, 0};
  SymbolDescriptor.Name = {SymbolName, 6};
  SymbolDescriptor.Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
  SymbolDescriptor.Visibility = NEVERC_MC_SYMBOL_VISIBILITY_DEFAULT;
  SymbolDescriptor.Type = NEVERC_MC_SYMBOL_TYPE_FUNCTION;
  SymbolDescriptor.Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
  SymbolDescriptor.Section = Section;
  SymbolDescriptor.Size = 4;
  SymbolDescriptor.Alignment = 4;
  NevercMCSymbolHandle Symbol{};
  ASSERT_EQ(API.CreateSymbol(API.Context, State.Scope.task().handle(), Mutation,
                             &SymbolDescriptor, &Symbol)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCExpressionDescriptor ExpressionDescriptor{};
  ExpressionDescriptor.Header = {
      sizeof(ExpressionDescriptor), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  ExpressionDescriptor.Kind = NEVERC_MC_EXPRESSION_SYMBOL_REF;
  ExpressionDescriptor.Symbol = Symbol;
  NevercMCExprHandle Expression{};
  ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                 Mutation, &ExpressionDescriptor, &Expression)
                .Code,
            NEVERC_STATUS_OK);

  std::array<uint8_t, 4> Bytes = {0, 0, 0, 0};
  NevercMCFragmentDescriptor FragmentDescriptor{};
  FragmentDescriptor.Header = {sizeof(FragmentDescriptor),
                               NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  FragmentDescriptor.Kind = NEVERC_MC_FRAGMENT_DATA;
  FragmentDescriptor.ExplicitOffset = 0;
  FragmentDescriptor.Alignment = 4;
  FragmentDescriptor.Contents = {Bytes.data(), Bytes.size()};
  NevercMCFragmentHandle Fragment{};
  ASSERT_EQ(API.CreateFragment(API.Context, State.Scope.task().handle(),
                               Mutation, Section, &FragmentDescriptor,
                               &Fragment)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCInstHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, State.SchemaToken, 10, &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCOperandValue Register{};
  Register.Header = {sizeof(Register), NEVERC_MC_API_MAJOR,
                     NEVERC_MC_API_MINOR, 0};
  Register.Kind = NEVERC_MC_OPERAND_REGISTER;
  Register.SchemaToken = State.SchemaToken;
  Register.Payload.Register = 20;
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(), Mutation,
                              Instruction, &Register)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AppendInstructionToFragment(
                API.Context, State.Scope.task().handle(), Mutation, Fragment,
                Instruction)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCFixupDescriptor FixupDescriptor{};
  FixupDescriptor.Header = {sizeof(FixupDescriptor), NEVERC_MC_API_MAJOR,
                            NEVERC_MC_API_MINOR, 0};
  FixupDescriptor.Expression = Expression;
  FixupDescriptor.Offset = 0;
  FixupDescriptor.Width = 32;
  FixupDescriptor.IsPCRelative = NEVERC_TRUE;
  FixupDescriptor.IsSigned = NEVERC_TRUE;
  FixupDescriptor.MayRelax = NEVERC_TRUE;
  FixupDescriptor.Kind = NEVERC_MC_FIXUP_DATA_4;
  NevercMCFixupHandle Fixup{};
  ASSERT_EQ(API.CreateFixup(API.Context, State.Scope.task().handle(), Mutation,
                            Fragment, &FixupDescriptor, &Fixup)
                .Code,
            NEVERC_STATUS_OK);

  SectionName[1] = 'b';
  SymbolName[0] = 'x';
  Bytes.fill(0xff);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(State.Unit.sectionCount(), 1U);
  EXPECT_EQ(State.Unit.symbolCount(), 1U);
  EXPECT_EQ(State.Unit.expressionCount(), 1U);
  EXPECT_EQ(State.Unit.fragmentCount(), 1U);
  EXPECT_EQ(State.Unit.fixupCount(), 1U);
  EXPECT_EQ(State.Unit.instructionCount(), 1U);

  NevercMCSectionInfo StaleInfo{};
  StaleInfo.Header = {sizeof(StaleInfo), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetSectionInfo(API.Context, State.Scope.task().handle(), Section,
                               &StaleInfo)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  auto CurrentUnit = State.Bridge->unit();
  ASSERT_TRUE(static_cast<bool>(CurrentUnit));
  NevercMCSectionHandle CurrentSection{};
  ASSERT_EQ(API.GetFirstSection(API.Context, State.Scope.task().handle(),
                                *CurrentUnit, &CurrentSection)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCSectionInfo SectionInfo{};
  SectionInfo.Header = {sizeof(SectionInfo), NEVERC_MC_API_MAJOR,
                        NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetSectionInfo(API.Context, State.Scope.task().handle(),
                               CurrentSection, &SectionInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(SectionInfo.Name.Data, SectionInfo.Name.Length),
            ".text");

  NevercMCSymbolHandle CurrentSymbol{};
  ASSERT_EQ(API.GetFirstSymbol(API.Context, State.Scope.task().handle(),
                               *CurrentUnit, &CurrentSymbol)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCSymbolInfo SymbolInfo{};
  SymbolInfo.Header = {sizeof(SymbolInfo), NEVERC_MC_API_MAJOR,
                       NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetSymbolInfo(API.Context, State.Scope.task().handle(),
                              CurrentSymbol, &SymbolInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(SymbolInfo.Name.Data, SymbolInfo.Name.Length),
            "_start");

  NevercMCFragmentHandle CurrentFragment{};
  ASSERT_EQ(API.GetFirstFragment(API.Context, State.Scope.task().handle(),
                                 CurrentSection, &CurrentFragment)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCFragmentInfo FragmentInfo{};
  FragmentInfo.Header = {sizeof(FragmentInfo), NEVERC_MC_API_MAJOR,
                         NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetFragmentInfo(API.Context, State.Scope.task().handle(),
                                CurrentFragment, &FragmentInfo)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(FragmentInfo.Contents.Length, 4U);
  EXPECT_EQ(FragmentInfo.Contents.Data[0], 0U);

  const std::string Structured = dumpPluginMCUnit(State.Unit);
  const std::string LLVMMC = dumpLLVMCompatibleMCUnit(State.Unit);
  EXPECT_NE(Structured.find(".text"), std::string::npos);
  EXPECT_NE(Structured.find("_start"), std::string::npos);
  EXPECT_NE(Structured.find("fixup"), std::string::npos);
  EXPECT_NE(LLVMMC.find(".section .text"), std::string::npos);
  EXPECT_NE(LLVMMC.find("_start:"), std::string::npos);
  EXPECT_NE(LLVMMC.find("opcode 100"), std::string::npos);
  EXPECT_NE(LLVMMC.find("fixup"), std::string::npos);
}

TEST(PluginMCBuilderTest, RejectsExpressionCycleAndRollsBackEverything) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercMCAPI &API = State.api();
  NevercMCMutationHandle Mutation = beginMutation(State);

  NevercMCExpressionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_MC_API_MAJOR,
                       NEVERC_MC_API_MINOR, 0};
  Descriptor.Kind = NEVERC_MC_EXPRESSION_UNARY;
  Descriptor.Operator = NEVERC_MC_UNARY_PLUS;
  NevercMCExprHandle First{};
  NevercMCExprHandle Second{};
  ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                 Mutation, &Descriptor, &First)
                .Code,
            NEVERC_STATUS_OK);
  Descriptor.Left = First;
  ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                 Mutation, &Descriptor, &Second)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetExpressionOperands(API.Context, State.Scope.task().handle(),
                                      Mutation, First, Second, {})
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(State.Unit.expressionCount(), 0U);

  NevercMCExpressionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetExpressionInfo(API.Context, State.Scope.task().handle(),
                                  First, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMCBuilderTest, BadFixupPreservesPreviouslyCommittedUnit) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercMCAPI &API = State.api();
  NevercMCMutationHandle Mutation = beginMutation(State);

  NevercMCSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor), NEVERC_MC_API_MAJOR,
                              NEVERC_MC_API_MINOR, 0};
  SectionDescriptor.Name = view(".data");
  SectionDescriptor.Alignment = 1;
  NevercMCSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                              Mutation, &SectionDescriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);
  std::array<uint8_t, 4> Bytes{};
  NevercMCFragmentDescriptor FragmentDescriptor{};
  FragmentDescriptor.Header = {sizeof(FragmentDescriptor),
                               NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  FragmentDescriptor.Kind = NEVERC_MC_FRAGMENT_DATA;
  FragmentDescriptor.ExplicitOffset = 0;
  FragmentDescriptor.Alignment = 1;
  FragmentDescriptor.Contents = {Bytes.data(), Bytes.size()};
  NevercMCFragmentHandle Fragment{};
  ASSERT_EQ(API.CreateFragment(API.Context, State.Scope.task().handle(),
                               Mutation, Section, &FragmentDescriptor,
                               &Fragment)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);

  const std::string Before = dumpPluginMCUnit(State.Unit);
  auto CurrentUnit = State.Bridge->unit();
  ASSERT_TRUE(static_cast<bool>(CurrentUnit));
  ASSERT_EQ(API.GetFirstSection(API.Context, State.Scope.task().handle(),
                                *CurrentUnit, &Section)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetFirstFragment(API.Context, State.Scope.task().handle(),
                                 Section, &Fragment)
                .Code,
            NEVERC_STATUS_OK);
  Mutation = {};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *CurrentUnit, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCExpressionDescriptor ExpressionDescriptor{};
  ExpressionDescriptor.Header = {
      sizeof(ExpressionDescriptor), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  ExpressionDescriptor.Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  ExpressionDescriptor.Constant = 7;
  NevercMCExprHandle Expression{};
  ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                 Mutation, &ExpressionDescriptor, &Expression)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCFixupDescriptor FixupDescriptor{};
  FixupDescriptor.Header = {sizeof(FixupDescriptor), NEVERC_MC_API_MAJOR,
                            NEVERC_MC_API_MINOR, 0};
  FixupDescriptor.Expression = Expression;
  FixupDescriptor.Offset = 4;
  FixupDescriptor.Width = 8;
  FixupDescriptor.Kind = NEVERC_MC_FIXUP_DATA_1;
  NevercMCFixupHandle Fixup{};
  ASSERT_EQ(API.CreateFixup(API.Context, State.Scope.task().handle(), Mutation,
                            Fragment, &FixupDescriptor, &Fixup)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(dumpPluginMCUnit(State.Unit), Before);
  EXPECT_EQ(State.Unit.fixupCount(), 0U);
  EXPECT_EQ(State.Unit.expressionCount(), 0U);
}

TEST(PluginMCBuilderTest, RejectsForeignTargetIdentityAtCommitGate) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  State.Unit.setTargetIdentity(
      NevercTargetID{UINT64_C(0xdead), UINT64_C(0xbeef)},
      State.Schema.Digest);
  NevercMCMutationHandle Mutation = beginMutation(State);
  EXPECT_EQ(State.api()
                .CommitMutation(State.api().Context, State.Scope.task().handle(),
                                Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST(PluginMCBuilderTest, ReordersEveryContainerAndEditsOperands) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercMCAPI &API = State.api();

  auto FirstSection = std::make_unique<PluginMCSection>();
  FirstSection->Name = ".first";
  auto SecondSection = std::make_unique<PluginMCSection>();
  SecondSection->Name = ".second";
  PluginMCSection *FirstSectionValue = FirstSection.get();
  PluginMCSection *SecondSectionValue = SecondSection.get();
  State.Unit.sections().push_back(std::move(FirstSection));
  State.Unit.sections().push_back(std::move(SecondSection));

  auto FirstSymbol = std::make_unique<PluginMCSymbol>();
  FirstSymbol->Name = "first";
  FirstSymbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
  FirstSymbol->Section = FirstSectionValue;
  auto SecondSymbol = std::make_unique<PluginMCSymbol>();
  SecondSymbol->Name = "second";
  SecondSymbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
  SecondSymbol->Section = SecondSectionValue;
  PluginMCSymbol *FirstSymbolValue = FirstSymbol.get();
  PluginMCSymbol *SecondSymbolValue = SecondSymbol.get();
  State.Unit.symbols().push_back(std::move(FirstSymbol));
  State.Unit.symbols().push_back(std::move(SecondSymbol));

  auto FirstExpression = std::make_unique<PluginMCExpression>();
  FirstExpression->Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  FirstExpression->Constant = 1;
  auto SecondExpression = std::make_unique<PluginMCExpression>();
  SecondExpression->Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  SecondExpression->Constant = 2;
  PluginMCExpression *FirstExpressionValue = FirstExpression.get();
  PluginMCExpression *SecondExpressionValue = SecondExpression.get();
  State.Unit.expressions().push_back(std::move(FirstExpression));
  State.Unit.expressions().push_back(std::move(SecondExpression));

  auto FirstFragment = std::make_unique<PluginMCFragment>();
  FirstFragment->Parent = FirstSectionValue;
  FirstFragment->Contents.resize(4);
  auto SecondFragment = std::make_unique<PluginMCFragment>();
  SecondFragment->Parent = SecondSectionValue;
  SecondFragment->Contents.resize(4);
  PluginMCFragment *FirstFragmentValue = FirstFragment.get();
  PluginMCFragment *SecondFragmentValue = SecondFragment.get();
  FirstSectionValue->Fragments.push_back(std::move(FirstFragment));
  SecondSectionValue->Fragments.push_back(std::move(SecondFragment));

  auto FirstFixup = std::make_unique<PluginMCFixup>();
  FirstFixup->Parent = FirstFragmentValue;
  FirstFixup->Expression = FirstExpressionValue;
  FirstFixup->Width = 8;
  FirstFixup->Kind = NEVERC_MC_FIXUP_DATA_1;
  auto SecondFixup = std::make_unique<PluginMCFixup>();
  SecondFixup->Parent = SecondFragmentValue;
  SecondFixup->Expression = SecondExpressionValue;
  SecondFixup->Width = 8;
  SecondFixup->Kind = NEVERC_MC_FIXUP_DATA_1;
  PluginMCFixup *FirstFixupValue = FirstFixup.get();
  SecondFragmentValue->Fixups.push_back(std::move(SecondFixup));
  FirstFragmentValue->Fixups.push_back(std::move(FirstFixup));

  auto ExistingInstruction = std::make_unique<MCInst>();
  ExistingInstruction->setOpcode(100);
  PluginMCUnit::InstructionStorage::value_type Existing =
      std::move(ExistingInstruction);
  MCInst *ExistingValue = Existing.get();
  FirstFragmentValue->Instructions.push_back(std::move(Existing));

  auto FirstSectionHandle = State.Bridge->wrapSection(*FirstSectionValue);
  auto SecondSectionHandle = State.Bridge->wrapSection(*SecondSectionValue);
  auto FirstSymbolHandle = State.Bridge->wrapSymbol(*FirstSymbolValue);
  auto SecondSymbolHandle = State.Bridge->wrapSymbol(*SecondSymbolValue);
  auto FirstFragmentHandle =
      State.Bridge->wrapFragment(*FirstFragmentValue);
  auto SecondFragmentHandle =
      State.Bridge->wrapFragment(*SecondFragmentValue);
  auto FirstFixupHandle = State.Bridge->wrapFixup(*FirstFixupValue);
  auto SecondFixupHandle =
      State.Bridge->wrapFixup(*SecondFragmentValue->Fixups.front());
  auto ExistingHandle = State.Bridge->wrapInstruction(*ExistingValue);
  ASSERT_TRUE(FirstSectionHandle && SecondSectionHandle &&
              FirstSymbolHandle && SecondSymbolHandle &&
              FirstFragmentHandle && SecondFragmentHandle &&
              FirstFixupHandle && SecondFixupHandle && ExistingHandle);

  NevercMCMutationHandle Mutation = beginMutation(State);
  ASSERT_EQ(API.MoveSectionBefore(
                API.Context, State.Scope.task().handle(), Mutation,
                *SecondSectionHandle, *FirstSectionHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveSymbolBefore(
                API.Context, State.Scope.task().handle(), Mutation,
                *SecondSymbolHandle, *FirstSymbolHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveFragmentBefore(
                API.Context, State.Scope.task().handle(), Mutation,
                *SecondFragmentHandle, *FirstFragmentHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveFixupBefore(
                API.Context, State.Scope.task().handle(), Mutation,
                *FirstFixupHandle, *SecondFixupHandle)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCInstHandle Inserted{};
  ASSERT_EQ(API.CreateInstruction(
                API.Context, State.Scope.task().handle(), Mutation,
                State.SchemaToken, 10, &Inserted)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCOperandValue Register{};
  Register.Header = {sizeof(Register), NEVERC_MC_API_MAJOR,
                     NEVERC_MC_API_MINOR, 0};
  Register.Kind = NEVERC_MC_OPERAND_REGISTER;
  Register.SchemaToken = State.SchemaToken;
  Register.Payload.Register = 20;
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Inserted, &Register)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCOperandValue Immediate{};
  Immediate.Header = {sizeof(Immediate), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
  Immediate.Kind = NEVERC_MC_OPERAND_IMMEDIATE;
  Immediate.Payload.Immediate = 7;
  NevercMCOperandHandle Operand{};
  ASSERT_EQ(API.InsertOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Inserted, 0, &Immediate, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseOperand(API.Context, State.Scope.task().handle(),
                             Mutation, Inserted, 1)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.InsertInstructionBefore(
                API.Context, State.Scope.task().handle(), Mutation,
                *ExistingHandle, Inserted)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(State.Unit.sections().front().get(), SecondSectionValue);
  EXPECT_EQ(State.Unit.symbols().front().get(), SecondSymbolValue);
  ASSERT_EQ(FirstSectionValue->Fragments.size(), 2U);
  EXPECT_EQ(FirstSectionValue->Fragments.front().get(),
            SecondFragmentValue);
  EXPECT_EQ(SecondFragmentValue->Parent, FirstSectionValue);
  ASSERT_EQ(SecondFragmentValue->Fixups.size(), 2U);
  EXPECT_EQ(SecondFragmentValue->Fixups.front().get(), FirstFixupValue);
  EXPECT_EQ(FirstFixupValue->Parent, SecondFragmentValue);
  ASSERT_EQ(FirstFragmentValue->Instructions.size(), 2U);
  const MCInst &FirstInstruction =
      *FirstFragmentValue->Instructions.front();
  EXPECT_EQ(FirstInstruction.getOpcode(), 100U);
  ASSERT_EQ(FirstInstruction.getNumOperands(), 1U);
  EXPECT_EQ(FirstInstruction.getOperand(0).getImm(), 7);
}

TEST(PluginMCBuilderTest, EraseThenAbandonRestoresFullOwnershipGraph) {
  BuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercMCAPI &API = State.api();

  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = ".rollback";
  PluginMCSection *SectionValue = Section.get();
  State.Unit.sections().push_back(std::move(Section));
  auto Symbol = std::make_unique<PluginMCSymbol>();
  Symbol->Name = "rollback";
  Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
  Symbol->Section = SectionValue;
  PluginMCSymbol *SymbolValue = Symbol.get();
  State.Unit.symbols().push_back(std::move(Symbol));
  auto Expression = std::make_unique<PluginMCExpression>();
  Expression->Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  PluginMCExpression *ExpressionValue = Expression.get();
  State.Unit.expressions().push_back(std::move(Expression));
  auto Fragment = std::make_unique<PluginMCFragment>();
  Fragment->Parent = SectionValue;
  Fragment->Contents.resize(1);
  PluginMCFragment *FragmentValue = Fragment.get();
  SectionValue->Fragments.push_back(std::move(Fragment));
  auto Instruction = std::make_unique<MCInst>();
  Instruction->setOpcode(100);
  MCInst *InstructionValue = Instruction.get();
  FragmentValue->Instructions.push_back(std::move(Instruction));
  auto Fixup = std::make_unique<PluginMCFixup>();
  Fixup->Parent = FragmentValue;
  Fixup->Expression = ExpressionValue;
  Fixup->Width = 8;
  Fixup->Kind = NEVERC_MC_FIXUP_DATA_1;
  PluginMCFixup *FixupValue = Fixup.get();
  FragmentValue->Fixups.push_back(std::move(Fixup));
  const std::string Before = dumpPluginMCUnit(State.Unit);

  auto SectionHandle = State.Bridge->wrapSection(*SectionValue);
  auto SymbolHandle = State.Bridge->wrapSymbol(*SymbolValue);
  auto ExpressionHandle = State.Bridge->wrapExpression(*ExpressionValue);
  auto FragmentHandle = State.Bridge->wrapFragment(*FragmentValue);
  auto InstructionHandle =
      State.Bridge->wrapInstruction(*InstructionValue);
  auto FixupHandle = State.Bridge->wrapFixup(*FixupValue);
  ASSERT_TRUE(SectionHandle && SymbolHandle && ExpressionHandle &&
              FragmentHandle && InstructionHandle && FixupHandle);

  NevercMCMutationHandle Mutation = beginMutation(State);
  ASSERT_EQ(API.EraseFixup(API.Context, State.Scope.task().handle(), Mutation,
                           *FixupHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseInstruction(API.Context, State.Scope.task().handle(),
                                 Mutation, *InstructionHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseFragment(API.Context, State.Scope.task().handle(),
                              Mutation, *FragmentHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseExpression(API.Context, State.Scope.task().handle(),
                                Mutation, *ExpressionHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseSymbol(API.Context, State.Scope.task().handle(), Mutation,
                            *SymbolHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseSection(API.Context, State.Scope.task().handle(), Mutation,
                             *SectionHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AbandonMutation(API.Context, State.Scope.task().handle(),
                                Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(dumpPluginMCUnit(State.Unit), Before);
  EXPECT_EQ(State.Unit.sectionCount(), 1U);
  EXPECT_EQ(State.Unit.symbolCount(), 1U);
  EXPECT_EQ(State.Unit.expressionCount(), 1U);
  EXPECT_EQ(State.Unit.fragmentCount(), 1U);
  EXPECT_EQ(State.Unit.instructionCount(), 1U);
  EXPECT_EQ(State.Unit.fixupCount(), 1U);
  NevercMCSectionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetSectionInfo(API.Context, State.Scope.task().handle(),
                               *SectionHandle, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMCBuilderTest, RejectsOverlapsAndOwnerlessExtensions) {
  {
    BuilderContext State;
    ASSERT_TRUE(State.initialize());
    const NevercMCAPI &API = State.api();
    NevercMCMutationHandle Mutation = beginMutation(State);
    NevercMCSectionDescriptor SectionDescriptor{};
    SectionDescriptor.Header = {sizeof(SectionDescriptor),
                                NEVERC_MC_API_MAJOR,
                                NEVERC_MC_API_MINOR, 0};
    SectionDescriptor.Name = view(".overlap");
    SectionDescriptor.Alignment = 1;
    NevercMCSectionHandle Section{};
    ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                                Mutation, &SectionDescriptor, &Section)
                  .Code,
              NEVERC_STATUS_OK);
    std::array<uint8_t, 4> Bytes{};
    NevercMCFragmentDescriptor FragmentDescriptor{};
    FragmentDescriptor.Header = {sizeof(FragmentDescriptor),
                                 NEVERC_MC_API_MAJOR,
                                 NEVERC_MC_API_MINOR, 0};
    FragmentDescriptor.Kind = NEVERC_MC_FRAGMENT_DATA;
    FragmentDescriptor.Alignment = 1;
    FragmentDescriptor.ExplicitOffset = 0;
    FragmentDescriptor.Contents = {Bytes.data(), Bytes.size()};
    NevercMCFragmentHandle First{};
    ASSERT_EQ(API.CreateFragment(API.Context, State.Scope.task().handle(),
                                 Mutation, Section, &FragmentDescriptor,
                                 &First)
                  .Code,
              NEVERC_STATUS_OK);
    FragmentDescriptor.ExplicitOffset = 2;
    NevercMCFragmentHandle Second{};
    ASSERT_EQ(API.CreateFragment(API.Context, State.Scope.task().handle(),
                                 Mutation, Section, &FragmentDescriptor,
                                 &Second)
                  .Code,
              NEVERC_STATUS_OK);
    EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                                 Mutation)
                  .Code,
              NEVERC_STATUS_VERIFICATION_FAILED);
    EXPECT_EQ(State.Unit.sectionCount(), 0U);
  }

  {
    BuilderContext State;
    ASSERT_TRUE(State.initialize());
    const NevercMCAPI &API = State.api();
    NevercMCMutationHandle Mutation = beginMutation(State);
    NevercMCExpressionDescriptor ConstantDescriptor{};
    ConstantDescriptor.Header = {sizeof(ConstantDescriptor),
                                 NEVERC_MC_API_MAJOR,
                                 NEVERC_MC_API_MINOR, 0};
    ConstantDescriptor.Kind = NEVERC_MC_EXPRESSION_CONSTANT;
    NevercMCExprHandle Constant{};
    ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                   Mutation, &ConstantDescriptor,
                                   &Constant)
                  .Code,
              NEVERC_STATUS_OK);
    NevercMCExpressionDescriptor TargetDescriptor{};
    TargetDescriptor.Header = {sizeof(TargetDescriptor),
                               NEVERC_MC_API_MAJOR,
                               NEVERC_MC_API_MINOR, 0};
    TargetDescriptor.Kind = NEVERC_MC_EXPRESSION_TARGET_VARIANT;
    TargetDescriptor.SchemaToken = State.SchemaToken;
    TargetDescriptor.TargetVariant = 40;
    TargetDescriptor.Left = Constant;
    NevercMCExprHandle Target{};
    ASSERT_EQ(API.CreateExpression(API.Context, State.Scope.task().handle(),
                                   Mutation, &TargetDescriptor, &Target)
                  .Code,
              NEVERC_STATUS_OK);
    EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                                 Mutation)
                  .Code,
              NEVERC_STATUS_VERIFICATION_FAILED);
    EXPECT_EQ(State.Unit.expressionCount(), 0U);
  }
}

} // namespace
