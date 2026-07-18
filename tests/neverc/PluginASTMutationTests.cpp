#include "neverc/Analyze/Sema.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Syntax/RunParser.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <string>

namespace neverc::test {
namespace {

using namespace llvm;
using namespace neverc::plugin;

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class RecordingMutationListener final : public TreeMutationListener {
public:
  void ReplacedDeclarationInitializer(const VarDecl *Declaration,
                                      const Expr *Previous,
                                      const Expr *Replacement) override {
    ++ReplacementCount;
    LastDeclaration = Declaration;
    LastPrevious = Previous;
    LastReplacement = Replacement;
  }

  unsigned ReplacementCount = 0;
  const VarDecl *LastDeclaration = nullptr;
  const Expr *LastPrevious = nullptr;
  const Expr *LastReplacement = nullptr;
};

class PluginASTMutationTest : public testing::Test {
protected:
  void SetUp() override {
    Services = std::make_unique<PluginProcessServices>(
        "neverc-plugin-ast-mutation-tests", LLVM_VERSION_MAJOR);
    ASSERT_FALSE(registerPluginFrontendInterface(*Services));
    ASSERT_FALSE(Services->interfaces().freeze());
    auto Loaded = Services->registry().load(NEVERC_TEST_MINIMAL_PLUGIN);
    ASSERT_TRUE(static_cast<bool>(Loaded))
        << takeErrorMessage(Loaded.takeError());
    const std::array<StringRef, 1> Selected = {"org.neverc.test.minimal"};
    auto CreatedPlan = makePluginActivationPlan(Services->registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(CreatedPlan))
        << takeErrorMessage(CreatedPlan.takeError());
    Plan = std::make_unique<PluginActivationPlan>(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(*Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(CreatedSession))
        << takeErrorMessage(CreatedSession.takeError());
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(CreatedTask))
        << takeErrorMessage(CreatedTask.takeError());
    Task = std::move(*CreatedTask);

    Compiler = std::make_unique<CompilerInstance>();
    Compiler->getInvocation().getTargetOpts().Triple =
        sys::getDefaultTargetTriple();
    Compiler->createDiagnostics();
    ASSERT_NE(Compiler->createFileManager(), nullptr);
    Compiler->createSourceManager(Compiler->getFileManager());
    ASSERT_TRUE(Compiler->createTarget());
    Compiler->createPrepEngine();

    constexpr llvm::StringLiteral Source = R"(
static int global = 7;
int value(void) { return global; }
)";
    SourceManager &SourceMgr = Compiler->getSourceManager();
    FileID MainFile = SourceMgr.createFileID(
        MemoryBuffer::getMemBufferCopy(Source, "ast-mutation-test.c"));
    ASSERT_TRUE(MainFile.isValid());
    SourceMgr.setMainFileID(MainFile);
    Compiler->createTreeContext();
    Compiler->setTreeConsumer(std::make_unique<TreeConsumer>());
    Compiler->createSema();
    RunParser(Compiler->getSema());

    Locations = std::make_unique<FrontendPluginBridge>(*Task, SourceMgr,
                                                       Compiler->getLangOpts());
    Prep = std::make_unique<PluginPrepBridge>(*Task, Compiler->getPrepEngine(),
                                              *Locations);
    ASSERT_FALSE(Prep->attachProcessInterface());
    AST = std::make_unique<PluginASTBridge>(*Task, Compiler->getTreeContext(),
                                            *Locations, Prep.get());
    ASSERT_FALSE(AST->attachProcessInterface());
    const NevercInterfaceID ASTInterface{NEVERC_INTERFACE_AST_HIGH,
                                         NEVERC_INTERFACE_AST_LOW};
    auto Queried = Services->interfaces().query(
        ASTInterface, NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(Queried))
        << takeErrorMessage(Queried.takeError());
    API = static_cast<const NevercASTAPI *>(Queried->Table);
    ASSERT_NE(API, nullptr);
  }

  void TearDown() override {
    API = nullptr;
    AST.reset();
    Prep.reset();
    Locations.reset();
    Compiler.reset();
    if (Task && !Task->isEnded())
      EXPECT_FALSE(Task->end());
    Task.reset();
    if (Session)
      EXPECT_FALSE(Session->end());
    Session.reset();
    Plan.reset();
    if (Services)
      EXPECT_FALSE(Services->shutdown());
    Services.reset();
  }

  PluginTaskContext &task() { return *Task; }
  const NevercASTAPI &api() const { return *API; }
  TreeContext &context() { return Compiler->getTreeContext(); }

  NevercDeclHandle findGlobal() {
    NevercDeclHandle Root{};
    EXPECT_EQ(
        api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
        NEVERC_STATUS_OK);
    uint64_t ChildCount = 0;
    EXPECT_EQ(
        api()
            .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
            .Code,
        NEVERC_STATUS_OK);
    for (uint64_t Index = 0; Index != ChildCount; ++Index) {
      NevercASTNodeHandle Child{};
      EXPECT_EQ(
          api()
              .GetChild(api().Context, task().handle(), Root, Index, &Child)
              .Code,
          NEVERC_STATUS_OK);
      NevercVarDeclInfo Variable{};
      Variable.Header.StructSize = sizeof(Variable);
      if (api()
              .GetVarDeclInfo(api().Context, task().handle(), Child, &Variable)
              .Code != NEVERC_STATUS_OK)
        continue;
      if (std::string(Variable.Name.Data,
                      static_cast<size_t>(Variable.Name.Length)) == "global")
        return Child;
    }
    return {};
  }

  NevercExprHandle buildIntegerForGlobal(uint64_t Number) {
    const NevercDeclHandle Global = findGlobal();
    if (neverc_handle_is_null(Global))
      return {};
    NevercVarDeclInfo Variable{};
    Variable.Header.StructSize = sizeof(Variable);
    if (api()
            .GetVarDeclInfo(api().Context, task().handle(), Global, &Variable)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercASTNodeInfo GlobalInfo{};
    GlobalInfo.Header.StructSize = sizeof(GlobalInfo);
    if (api()
            .GetNodeInfo(api().Context, task().handle(), Global, &GlobalInfo)
            .Code != NEVERC_STATUS_OK)
      return {};

    NevercASTBuilderHandle Builder{};
    if (api()
            .CreateASTBuilder(api().Context, task().handle(),
                              NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercASTValue Type{};
    Type.Header = {sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Type.Type = NEVERC_AST_VALUE_TYPE;
    Type.NodeValue = Variable.Type;
    if (api()
            .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                   NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &Type)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercASTValue Range{};
    Range.Header = {sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
                    0};
    Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
    Range.SourceRangeValue = GlobalInfo.SourceRange;
    if (api()
            .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                   NEVERC_AST_PROPERTY_AST_SOURCE_RANGE, &Range)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercAPIntView Integer{};
    Integer.Header = {sizeof(Integer), NEVERC_AST_API_MAJOR,
                      NEVERC_AST_API_MINOR, 0};
    Integer.Words = &Number;
    Integer.WordCount = 1;
    Integer.BitWidth = 32;
    if (api()
            .ASTBuilderSetIntegerValue(api().Context, task().handle(), Builder,
                                       &Integer)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercExprHandle Result{};
    const NevercStatus CommitStatus = api().ASTBuilderCommit(
        api().Context, task().handle(), Builder, &Result);
    const NevercStatus DestroyStatus =
        api().DestroyASTBuilder(api().Context, task().handle(), Builder);
    if (CommitStatus.Code != NEVERC_STATUS_OK ||
        DestroyStatus.Code != NEVERC_STATUS_OK)
      return {};
    return Result;
  }

private:
  std::unique_ptr<PluginProcessServices> Services;
  std::unique_ptr<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<CompilerInstance> Compiler;
  std::unique_ptr<FrontendPluginBridge> Locations;
  std::unique_ptr<PluginPrepBridge> Prep;
  std::unique_ptr<PluginASTBridge> AST;
  const NevercASTAPI *API = nullptr;
};

TEST_F(PluginASTMutationTest, BuildsIntegerLiteralThroughStableCAPI) {
  const NevercDeclHandle Global = findGlobal();
  ASSERT_FALSE(neverc_handle_is_null(Global));
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(
      api()
          .GetVarDeclInfo(api().Context, task().handle(), Global, &Variable)
          .Code,
      NEVERC_STATUS_OK);
  NevercASTNodeInfo GlobalInfo{};
  GlobalInfo.Header.StructSize = sizeof(GlobalInfo);
  ASSERT_EQ(
      api()
          .GetNodeInfo(api().Context, task().handle(), Global, &GlobalInfo)
          .Code,
      NEVERC_STATUS_OK);

  NevercASTBuilderHandle Builder{};
  ASSERT_EQ(api()
                .CreateASTBuilder(api().Context, task().handle(),
                                  NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder)
                .Code,
            NEVERC_STATUS_OK);

  NevercASTValue Type{};
  Type.Header = {sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Type.Type = NEVERC_AST_VALUE_TYPE;
  Type.NodeValue = Variable.Type;
  ASSERT_EQ(api()
                .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                       NEVERC_AST_PROPERTY_STMT_EXPR_TYPE,
                                       &Type)
                .Code,
            NEVERC_STATUS_OK);

  NevercASTValue Range{};
  Range.Header = {sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  Range.SourceRangeValue = GlobalInfo.SourceRange;
  ASSERT_EQ(api()
                .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                       NEVERC_AST_PROPERTY_AST_SOURCE_RANGE,
                                       &Range)
                .Code,
            NEVERC_STATUS_OK);

  const uint64_t Word = 42;
  NevercAPIntView Integer{};
  Integer.Header = {sizeof(Integer), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
                    0};
  Integer.Words = &Word;
  Integer.WordCount = 1;
  Integer.BitWidth = 32;
  ASSERT_EQ(api()
                .ASTBuilderSetIntegerValue(api().Context, task().handle(),
                                           Builder, &Integer)
                .Code,
            NEVERC_STATUS_OK);

  NevercASTNodeHandle Literal{};
  ASSERT_EQ(
      api()
          .ASTBuilderCommit(api().Context, task().handle(), Builder, &Literal)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Literal));

  NevercIntegerLiteralInfo LiteralInfo{};
  LiteralInfo.Header.StructSize = sizeof(LiteralInfo);
  ASSERT_EQ(api()
                .GetIntegerLiteralInfo(api().Context, task().handle(), Literal,
                                       &LiteralInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(LiteralInfo.BitWidth, 32U);
  ASSERT_EQ(LiteralInfo.WordCount, 1U);
  uint64_t LiteralWord = 0;
  ASSERT_EQ(api()
                .GetIntegerLiteralWord(api().Context, task().handle(), Literal,
                                       0, &LiteralWord)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(LiteralWord, 42U);
  EXPECT_EQ(
      api()
          .ASTBuilderCommit(api().Context, task().handle(), Builder, &Literal)
          .Code,
      NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(
      api().DestroyASTBuilder(api().Context, task().handle(), Builder).Code,
      NEVERC_STATUS_OK);
}

TEST_F(PluginASTMutationTest, BuildsBinaryExpressionFromSchemaChildSlots) {
  const NevercDeclHandle Global = findGlobal();
  ASSERT_FALSE(neverc_handle_is_null(Global));
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(
      api()
          .GetVarDeclInfo(api().Context, task().handle(), Global, &Variable)
          .Code,
      NEVERC_STATUS_OK);
  NevercASTNodeInfo GlobalInfo{};
  GlobalInfo.Header.StructSize = sizeof(GlobalInfo);
  ASSERT_EQ(
      api()
          .GetNodeInfo(api().Context, task().handle(), Global, &GlobalInfo)
          .Code,
      NEVERC_STATUS_OK);

  const auto BuildInteger = [&](uint64_t Number) {
    NevercASTBuilderHandle Builder{};
    EXPECT_EQ(api()
                  .CreateASTBuilder(api().Context, task().handle(),
                                    NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Type{};
    Type.Header = {sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Type.Type = NEVERC_AST_VALUE_TYPE;
    Type.NodeValue = Variable.Type;
    EXPECT_EQ(
        api()
            .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                   NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &Type)
            .Code,
        NEVERC_STATUS_OK);
    NevercASTValue Range{};
    Range.Header = {sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
                    0};
    Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
    Range.SourceRangeValue = GlobalInfo.SourceRange;
    EXPECT_EQ(
        api()
            .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                   NEVERC_AST_PROPERTY_AST_SOURCE_RANGE, &Range)
            .Code,
        NEVERC_STATUS_OK);
    NevercAPIntView Integer{};
    Integer.Header = {sizeof(Integer), NEVERC_AST_API_MAJOR,
                      NEVERC_AST_API_MINOR, 0};
    Integer.Words = &Number;
    Integer.WordCount = 1;
    Integer.BitWidth = 32;
    EXPECT_EQ(api()
                  .ASTBuilderSetIntegerValue(api().Context, task().handle(),
                                             Builder, &Integer)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTNodeHandle Result{};
    EXPECT_EQ(
        api()
            .ASTBuilderCommit(api().Context, task().handle(), Builder, &Result)
            .Code,
        NEVERC_STATUS_OK);
    EXPECT_EQ(
        api().DestroyASTBuilder(api().Context, task().handle(), Builder).Code,
        NEVERC_STATUS_OK);
    return Result;
  };

  const NevercExprHandle Left = BuildInteger(40);
  const NevercExprHandle Right = BuildInteger(2);
  ASSERT_FALSE(neverc_handle_is_null(Left));
  ASSERT_FALSE(neverc_handle_is_null(Right));

  NevercASTBuilderHandle Builder{};
  ASSERT_EQ(api()
                .CreateASTBuilder(api().Context, task().handle(),
                                  NEVERC_STMT_KIND_BINARY_OPERATOR, &Builder)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTValue Type{};
  Type.Header = {sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Type.Type = NEVERC_AST_VALUE_TYPE;
  Type.NodeValue = Variable.Type;
  ASSERT_EQ(api()
                .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                       NEVERC_AST_PROPERTY_STMT_EXPR_TYPE,
                                       &Type)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTValue Range{};
  Range.Header = {sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  Range.SourceRangeValue = GlobalInfo.SourceRange;
  ASSERT_EQ(api()
                .ASTBuilderSetProperty(api().Context, task().handle(), Builder,
                                       NEVERC_AST_PROPERTY_AST_SOURCE_RANGE,
                                       &Range)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(api()
                .ASTBuilderSetBinaryOperatorKind(api().Context, task().handle(),
                                                 Builder,
                                                 NEVERC_BINARY_OPERATOR_ADD)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(api()
                .ASTBuilderSetChild(
                    api().Context, task().handle(), Builder,
                    NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_LHS, 0, Left)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(api()
                .ASTBuilderSetChild(
                    api().Context, task().handle(), Builder,
                    NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_RHS, 0, Right)
                .Code,
            NEVERC_STATUS_OK);

  NevercASTNodeHandle Expression{};
  ASSERT_EQ(api()
                .ASTBuilderCommit(api().Context, task().handle(), Builder,
                                  &Expression)
                .Code,
            NEVERC_STATUS_OK);
  NevercBinaryOperatorInfo Binary{};
  Binary.Header.StructSize = sizeof(Binary);
  ASSERT_EQ(api()
                .GetBinaryOperatorInfo(api().Context, task().handle(),
                                       Expression, &Binary)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(Binary.Operator.Data,
                        static_cast<size_t>(Binary.Operator.Length)),
            "+");
  EXPECT_FALSE(neverc_handle_is_null(Binary.Left));
  EXPECT_FALSE(neverc_handle_is_null(Binary.Right));
  EXPECT_EQ(
      api().DestroyASTBuilder(api().Context, task().handle(), Builder).Code,
      NEVERC_STATUS_OK);
}

TEST_F(PluginASTMutationTest, CommitsVariableInitializerReplacementAtomically) {
  RecordingMutationListener Listener;
  context().setTreeMutationListener(&Listener);
  const NevercDeclHandle Global = findGlobal();
  const NevercExprHandle Replacement = buildIntegerForGlobal(42);
  ASSERT_FALSE(neverc_handle_is_null(Global));
  ASSERT_FALSE(neverc_handle_is_null(Replacement));

  NevercVarDeclInfo Before{};
  Before.Header.StructSize = sizeof(Before);
  ASSERT_EQ(api()
                .GetVarDeclInfo(api().Context, task().handle(), Global, &Before)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Before.Initializer));

  NevercASTMutationHandle Mutation{};
  ASSERT_EQ(
      api().BeginASTMutation(api().Context, task().handle(), &Mutation).Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(api()
                .ASTMutationReplaceChild(
                    api().Context, task().handle(), Mutation, Global,
                    NEVERC_AST_CHILD_SLOT_DECL_VAR_INITIALIZER, 0, Replacement)
                .Code,
            NEVERC_STATUS_OK);

  NevercVarDeclInfo During{};
  During.Header.StructSize = sizeof(During);
  ASSERT_EQ(api()
                .GetVarDeclInfo(api().Context, task().handle(), Global, &During)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(During.Initializer.Owner, Before.Initializer.Owner);
  EXPECT_EQ(During.Initializer.Value, Before.Initializer.Value);

  ASSERT_EQ(
      api().CommitASTMutation(api().Context, task().handle(), Mutation).Code,
      NEVERC_STATUS_OK);
  NevercVarDeclInfo After{};
  After.Header.StructSize = sizeof(After);
  ASSERT_EQ(
      api().GetVarDeclInfo(api().Context, task().handle(), Global, &After).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(After.Initializer.Owner, Replacement.Owner);
  EXPECT_EQ(After.Initializer.Value, Replacement.Value);
  uint64_t Word = 0;
  ASSERT_EQ(api()
                .GetIntegerLiteralWord(api().Context, task().handle(),
                                       After.Initializer, 0, &Word)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Word, 42U);
  EXPECT_EQ(Listener.ReplacementCount, 1U);
  EXPECT_NE(Listener.LastDeclaration, nullptr);
  EXPECT_NE(Listener.LastPrevious, nullptr);
  EXPECT_NE(Listener.LastReplacement, nullptr);
  EXPECT_EQ(
      api().CommitASTMutation(api().Context, task().handle(), Mutation).Code,
      NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(
      api().DestroyASTMutation(api().Context, task().handle(), Mutation).Code,
      NEVERC_STATUS_OK);
  context().setTreeMutationListener(nullptr);
}

} // namespace
} // namespace neverc::test
