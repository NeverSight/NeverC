#include "neverc/Analyze/Sema.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
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
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <array>
#include <limits>
#include <memory>
#include <string>

namespace neverc::test {
namespace {

using namespace llvm;
using namespace neverc::plugin;

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

std::string identifierName(const NevercPrepAPI &API, NevercTaskHandle Task,
                           NevercIdentifierHandle Identifier) {
  NevercIdentifierInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  if (API.GetIdentifierInfo(API.Context, Task, Identifier, &Info).Code !=
      NEVERC_STATUS_OK)
    return {};
  return std::string(Info.Name.Data, static_cast<size_t>(Info.Name.Length));
}

NevercASTNodeHandle findFirstNodeKind(const NevercASTAPI &API,
                                      NevercTaskHandle Task,
                                      NevercASTNodeHandle Root,
                                      NevercASTNodeKind Kind) {
  NevercASTNodeInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  if (API.GetNodeInfo(API.Context, Task, Root, &Info).Code != NEVERC_STATUS_OK)
    return {};
  if (Info.Kind == Kind)
    return Root;

  uint64_t ChildCount = 0;
  if (API.GetChildCount(API.Context, Task, Root, &ChildCount).Code !=
      NEVERC_STATUS_OK)
    return {};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    if (API.GetChild(API.Context, Task, Root, Index, &Child).Code !=
        NEVERC_STATUS_OK)
      return {};
    NevercASTNodeHandle Match = findFirstNodeKind(API, Task, Child, Kind);
    if (!neverc_handle_is_null(Match))
      return Match;
  }
  return {};
}

class PluginASTReadTest : public testing::Test {
protected:
  void SetUp() override {
    Services = std::make_unique<PluginProcessServices>(
        "neverc-plugin-ast-read-tests", LLVM_VERSION_MAJOR);
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

    DiagnosticBuffer = std::make_unique<TextDiagnosticBuffer>();
    Compiler = std::make_unique<CompilerInstance>();
    Compiler->getInvocation().getTargetOpts().Triple =
        sys::getDefaultTargetTriple();
    Compiler->createDiagnostics(DiagnosticBuffer.get(),
                                /*ShouldOwnClient=*/false);
    ASSERT_NE(Compiler->createFileManager(), nullptr);
    Compiler->createSourceManager(Compiler->getFileManager());
    ASSERT_TRUE(Compiler->createTarget());
    Compiler->createPrepEngine();

    constexpr llvm::StringLiteral Source = R"(
struct Pair { int value; };
__attribute__((deprecated("use replacement"))) static int global = 7;
int add(int lhs, int rhs) { return lhs + rhs; }
int use(void) { return add(global, 1); }
)";
    SourceManager &SourceMgr = Compiler->getSourceManager();
    FileID MainFile = SourceMgr.createFileID(
        MemoryBuffer::getMemBufferCopy(Source, "ast-read-test.c"));
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
    NevercInterfaceID ASTInterface{NEVERC_INTERFACE_AST_HIGH,
                                   NEVERC_INTERFACE_AST_LOW};
    auto Queried = Services->interfaces().query(
        ASTInterface, NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(Queried))
        << takeErrorMessage(Queried.takeError());
    ASSERT_NE(Queried->Table, nullptr);
    QueriedAPI = static_cast<const NevercASTAPI *>(Queried->Table);
  }

  void TearDown() override {
    QueriedAPI = nullptr;
    AST.reset();
    Prep.reset();
    Locations.reset();
    Compiler.reset();
    DiagnosticBuffer.reset();
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
  const NevercASTAPI &api() const { return *QueriedAPI; }
  const NevercPrepAPI &prepAPI() const { return Prep->prepAPI(); }

private:
  std::unique_ptr<PluginProcessServices> Services;
  std::unique_ptr<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<TextDiagnosticBuffer> DiagnosticBuffer;
  std::unique_ptr<CompilerInstance> Compiler;
  std::unique_ptr<FrontendPluginBridge> Locations;
  std::unique_ptr<PluginPrepBridge> Prep;
  std::unique_ptr<PluginASTBridge> AST;
  const NevercASTAPI *QueriedAPI = nullptr;
};

TEST_F(PluginASTReadTest, TraversesTranslationUnitWithStableKindsAndParents) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(Root));

  NevercASTNodeInfo RootInfo{};
  RootInfo.Header.StructSize = sizeof(RootInfo);
  ASSERT_EQ(
      api().GetNodeInfo(api().Context, task().handle(), Root, &RootInfo).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(RootInfo.Kind, NEVERC_DECL_KIND_TRANSLATION_UNIT);
  EXPECT_EQ(RootInfo.Domain, NEVERC_AST_SCHEMA_DOMAIN_DECL);
  EXPECT_TRUE(neverc_handle_is_null(RootInfo.Parent));

  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_GE(ChildCount, 4U);

  bool FoundAdd = false;
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);

    NevercASTNodeHandle Parent{};
    ASSERT_EQ(
        api().GetParent(api().Context, task().handle(), Child, &Parent).Code,
        NEVERC_STATUS_OK);
    EXPECT_TRUE(sameHandle(Parent, Root));

    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    NevercStatus NameStatus =
        api().GetProperty(api().Context, task().handle(), Child,
                          NEVERC_AST_PROPERTY_DECL_NAME, &Name);
    if (NameStatus.Code != NEVERC_STATUS_OK)
      continue;
    ASSERT_EQ(Name.Type, NEVERC_AST_VALUE_IDENTIFIER);
    std::string Text =
        identifierName(prepAPI(), task().handle(), Name.NodeValue);
    if (Text == "add")
      FoundAdd = true;
  }
  EXPECT_TRUE(FoundAdd);
}

TEST_F(PluginASTReadTest, ReadsFunctionAndTypeDetailsWithoutInternalLayouts) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);

  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);

  NevercDeclHandle Add{};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    if (api()
            .GetProperty(api().Context, task().handle(), Child,
                         NEVERC_AST_PROPERTY_DECL_NAME, &Name)
            .Code != NEVERC_STATUS_OK)
      continue;
    if (identifierName(prepAPI(), task().handle(), Name.NodeValue) == "add") {
      Add = Child;
      break;
    }
  }
  ASSERT_FALSE(neverc_handle_is_null(Add));

  NevercFunctionDeclInfo Function{};
  Function.Header.StructSize = sizeof(Function);
  ASSERT_EQ(
      api()
          .GetFunctionDeclInfo(api().Context, task().handle(), Add, &Function)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(Function.Name.Data,
                        static_cast<size_t>(Function.Name.Length)),
            "add");
  EXPECT_EQ(Function.ParameterCount, 2U);
  EXPECT_EQ(Function.IsVariadic, NEVERC_FALSE);
  EXPECT_EQ(Function.IsDefinition, NEVERC_TRUE);
  EXPECT_FALSE(neverc_handle_is_null(Function.Body));
  EXPECT_FALSE(neverc_handle_is_null(Function.FunctionType));
  EXPECT_FALSE(neverc_handle_is_null(Function.ReturnType));

  NevercDeclHandle FirstParameter{};
  ASSERT_EQ(api()
                .GetFunctionDeclParameter(api().Context, task().handle(), Add,
                                          0, &FirstParameter)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTValue ParameterName{};
  ParameterName.Header.StructSize = sizeof(ParameterName);
  ASSERT_EQ(api()
                .GetProperty(api().Context, task().handle(), FirstParameter,
                             NEVERC_AST_PROPERTY_DECL_NAME, &ParameterName)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ParameterName.Type, NEVERC_AST_VALUE_IDENTIFIER);
  EXPECT_EQ(identifierName(prepAPI(), task().handle(), ParameterName.NodeValue),
            "lhs");

  NevercTypeInfo ReturnType{};
  ReturnType.Header.StructSize = sizeof(ReturnType);
  ASSERT_EQ(api()
                .GetTypeInfo(api().Context, task().handle(),
                             Function.ReturnType, &ReturnType)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ReturnType.Kind, NEVERC_TYPE_KIND_BUILTIN);
  EXPECT_EQ(ReturnType.QualifierFlags, 0U);
  EXPECT_EQ(ReturnType.SizeInBits, 32U);
  EXPECT_EQ(ReturnType.AlignmentInBits, 32U);
  EXPECT_TRUE(ReturnType.Flags & NEVERC_TYPE_FLAG_HAS_KNOWN_LAYOUT);
  EXPECT_EQ(std::string(ReturnType.Name.Data,
                        static_cast<size_t>(ReturnType.Name.Length)),
            "int");

  NevercTypeInfo FunctionType{};
  FunctionType.Header.StructSize = sizeof(FunctionType);
  ASSERT_EQ(api()
                .GetTypeInfo(api().Context, task().handle(),
                             Function.FunctionType, &FunctionType)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(FunctionType.Flags & NEVERC_TYPE_FLAG_FUNCTION);
  EXPECT_EQ(FunctionType.ElementCount, 2U);
  EXPECT_TRUE(sameHandle(FunctionType.RelatedType, Function.ReturnType));

  NevercTypeHandle ParameterType{};
  ASSERT_EQ(api()
                .GetTypeElement(api().Context, task().handle(),
                                Function.FunctionType, 0, &ParameterType)
                .Code,
            NEVERC_STATUS_OK);
  NevercTypeInfo ParameterTypeInfo{};
  ParameterTypeInfo.Header.StructSize = sizeof(ParameterTypeInfo);
  ASSERT_EQ(api()
                .GetTypeInfo(api().Context, task().handle(), ParameterType,
                             &ParameterTypeInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ParameterTypeInfo.Kind, NEVERC_TYPE_KIND_BUILTIN);
}

TEST_F(PluginASTReadTest, BatchesNodeAndPropertyQueriesWithCallerStrides) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);

  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);

  std::array<NevercASTNodeHandle, 3> Nodes{};
  std::array<std::string, 3> ExpectedNames{};
  uint64_t QueryCount = 0;
  for (uint64_t Index = 0; Index != ChildCount && QueryCount != Nodes.size();
       ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    if (api()
            .GetProperty(api().Context, task().handle(), Child,
                         NEVERC_AST_PROPERTY_DECL_NAME, &Name)
            .Code != NEVERC_STATUS_OK)
      continue;
    const std::string Text =
        identifierName(prepAPI(), task().handle(), Name.NodeValue);
    if (Text.empty())
      continue;
    Nodes[QueryCount] = Child;
    ExpectedNames[QueryCount] = Text;
    ++QueryCount;
  }
  ASSERT_EQ(QueryCount, Nodes.size());

  struct NodeInfoSlot {
    NevercASTNodeInfo Info;
    uint64_t Canary;
  };
  std::array<NodeInfoSlot, 3> NodeInfos{};
  for (NodeInfoSlot &Slot : NodeInfos) {
    Slot.Info.Header.StructSize = sizeof(Slot.Info);
    Slot.Canary = UINT64_C(0x51a7c0de51a7c0de);
  }
  ASSERT_EQ(api()
                .GetNodeInfoBatch(api().Context, task().handle(), Nodes.data(),
                                  QueryCount, &NodeInfos[0].Info,
                                  NodeInfos.size(), sizeof(NodeInfoSlot))
                .Code,
            NEVERC_STATUS_OK);
  for (const NodeInfoSlot &Slot : NodeInfos) {
    EXPECT_EQ(Slot.Info.Domain, NEVERC_AST_SCHEMA_DOMAIN_DECL);
    EXPECT_TRUE(sameHandle(Slot.Info.Parent, Root));
    EXPECT_EQ(Slot.Canary, UINT64_C(0x51a7c0de51a7c0de));
  }

  struct ASTValueSlot {
    NevercASTValue Value;
    uint64_t Canary;
  };
  const std::array<NevercASTPropertyID, 3> Properties = {
      NEVERC_AST_PROPERTY_DECL_NAME, NEVERC_AST_PROPERTY_DECL_NAME,
      NEVERC_AST_PROPERTY_DECL_NAME};
  std::array<ASTValueSlot, 3> Values{};
  for (ASTValueSlot &Slot : Values) {
    Slot.Value.Header.StructSize = sizeof(Slot.Value);
    Slot.Canary = UINT64_C(0xa11ce5a1a11ce5a1);
  }
  ASSERT_EQ(api()
                .GetPropertyBatch(api().Context, task().handle(), Nodes.data(),
                                  Properties.data(), QueryCount,
                                  &Values[0].Value, Values.size(),
                                  sizeof(ASTValueSlot))
                .Code,
            NEVERC_STATUS_OK);
  for (size_t Index = 0; Index != Values.size(); ++Index) {
    EXPECT_EQ(Values[Index].Value.Type, NEVERC_AST_VALUE_IDENTIFIER);
    EXPECT_EQ(identifierName(prepAPI(), task().handle(),
                             Values[Index].Value.NodeValue),
              ExpectedNames[Index]);
    EXPECT_EQ(Values[Index].Canary, UINT64_C(0xa11ce5a1a11ce5a1));
  }

  EXPECT_EQ(api()
                .GetNodeInfoBatch(api().Context, task().handle(), Nodes.data(),
                                  QueryCount, &NodeInfos[0].Info,
                                  NodeInfos.size(),
                                  sizeof(NevercASTNodeInfo) - 1)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
  const uint64_t OverflowingCount =
      static_cast<uint64_t>(std::numeric_limits<size_t>::max() /
                            sizeof(NevercASTNodeInfo)) +
      1;
  EXPECT_EQ(api()
                .GetNodeInfoBatch(api().Context, task().handle(), Nodes.data(),
                                  OverflowingCount, &NodeInfos[0].Info,
                                  OverflowingCount, sizeof(NevercASTNodeInfo))
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
}

TEST_F(PluginASTReadTest, EnumeratesAttributesAndReadsTypedArguments) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);
  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);

  NevercDeclHandle Global{};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    if (api()
            .GetProperty(api().Context, task().handle(), Child,
                         NEVERC_AST_PROPERTY_DECL_NAME, &Name)
            .Code != NEVERC_STATUS_OK)
      continue;
    if (identifierName(prepAPI(), task().handle(), Name.NodeValue) ==
        "global") {
      Global = Child;
      break;
    }
  }
  ASSERT_FALSE(neverc_handle_is_null(Global));

  uint64_t AttributeCount = 0;
  ASSERT_EQ(api()
                .GetDeclAttributeCount(api().Context, task().handle(), Global,
                                       &AttributeCount)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(AttributeCount, 1U);

  NevercAttrHandle Attribute{};
  ASSERT_EQ(api()
                .GetDeclAttribute(api().Context, task().handle(), Global, 0,
                                  &Attribute)
                .Code,
            NEVERC_STATUS_OK);
  NevercAttrInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  ASSERT_EQ(
      api().GetAttrInfo(api().Context, task().handle(), Attribute, &Info).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Kind, NEVERC_ATTR_KIND_DEPRECATED);
  EXPECT_EQ(std::string(Info.Spelling.Data,
                        static_cast<size_t>(Info.Spelling.Length)),
            "deprecated");
  EXPECT_EQ(Info.IsImplicit, NEVERC_FALSE);
  EXPECT_EQ(Info.IsInherited, NEVERC_FALSE);

  NevercASTValue Message{};
  Message.Header.StructSize = sizeof(Message);
  ASSERT_EQ(api()
                .GetProperty(api().Context, task().handle(), Attribute,
                             NEVERC_AST_PROPERTY_ATTR_DEPRECATED_MESSAGE,
                             &Message)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Message.Type, NEVERC_AST_VALUE_STRING);
  EXPECT_EQ(std::string(Message.StringValue.Data,
                        static_cast<size_t>(Message.StringValue.Length)),
            "use replacement");
}

TEST_F(PluginASTReadTest, ReadsVariableAndRecordConvenienceViews) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);
  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);

  NevercDeclHandle Global{};
  NevercDeclHandle Pair{};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    if (api()
            .GetProperty(api().Context, task().handle(), Child,
                         NEVERC_AST_PROPERTY_DECL_NAME, &Name)
            .Code != NEVERC_STATUS_OK)
      continue;
    const std::string Text =
        identifierName(prepAPI(), task().handle(), Name.NodeValue);
    if (Text == "global")
      Global = Child;
    else if (Text == "Pair")
      Pair = Child;
  }
  ASSERT_FALSE(neverc_handle_is_null(Global));
  ASSERT_FALSE(neverc_handle_is_null(Pair));

  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(
      api()
          .GetVarDeclInfo(api().Context, task().handle(), Global, &Variable)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(Variable.Name.Data,
                        static_cast<size_t>(Variable.Name.Length)),
            "global");
  EXPECT_FALSE(neverc_handle_is_null(Variable.Type));
  EXPECT_FALSE(neverc_handle_is_null(Variable.Initializer));
  EXPECT_EQ(Variable.IsDefinition, NEVERC_TRUE);
  EXPECT_EQ(Variable.HasGlobalStorage, NEVERC_TRUE);

  NevercRecordDeclInfo Record{};
  Record.Header.StructSize = sizeof(Record);
  ASSERT_EQ(
      api()
          .GetRecordDeclInfo(api().Context, task().handle(), Pair, &Record)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(
      std::string(Record.Name.Data, static_cast<size_t>(Record.Name.Length)),
      "Pair");
  EXPECT_EQ(Record.FieldCount, 1U);
  EXPECT_EQ(Record.IsComplete, NEVERC_TRUE);
  EXPECT_EQ(Record.IsUnion, NEVERC_FALSE);
  EXPECT_EQ(Record.HasFlexibleArrayMember, NEVERC_FALSE);
}

TEST_F(PluginASTReadTest, ReadsCommonExpressionConvenienceViews) {
  NevercDeclHandle Root{};
  ASSERT_EQ(
      api().GetTranslationUnit(api().Context, task().handle(), &Root).Code,
      NEVERC_STATUS_OK);
  uint64_t ChildCount = 0;
  ASSERT_EQ(
      api()
          .GetChildCount(api().Context, task().handle(), Root, &ChildCount)
          .Code,
      NEVERC_STATUS_OK);

  NevercDeclHandle Add{};
  NevercDeclHandle Use{};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    ASSERT_EQ(api()
                  .GetChild(api().Context, task().handle(), Root, Index, &Child)
                  .Code,
              NEVERC_STATUS_OK);
    NevercASTValue Name{};
    Name.Header.StructSize = sizeof(Name);
    if (api()
            .GetProperty(api().Context, task().handle(), Child,
                         NEVERC_AST_PROPERTY_DECL_NAME, &Name)
            .Code != NEVERC_STATUS_OK)
      continue;
    const std::string Text =
        identifierName(prepAPI(), task().handle(), Name.NodeValue);
    if (Text == "add")
      Add = Child;
    else if (Text == "use")
      Use = Child;
  }
  ASSERT_FALSE(neverc_handle_is_null(Add));
  ASSERT_FALSE(neverc_handle_is_null(Use));

  NevercFunctionDeclInfo AddFunction{};
  AddFunction.Header.StructSize = sizeof(AddFunction);
  ASSERT_EQ(api()
                .GetFunctionDeclInfo(api().Context, task().handle(), Add,
                                     &AddFunction)
                .Code,
            NEVERC_STATUS_OK);
  NevercCompoundStmtInfo AddBody{};
  AddBody.Header.StructSize = sizeof(AddBody);
  ASSERT_EQ(api()
                .GetCompoundStmtInfo(api().Context, task().handle(),
                                     AddFunction.Body, &AddBody)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(AddBody.StatementCount, 1U);

  NevercASTNodeHandle Binary =
      findFirstNodeKind(api(), task().handle(), AddFunction.Body,
                        NEVERC_STMT_KIND_BINARY_OPERATOR);
  ASSERT_FALSE(neverc_handle_is_null(Binary));
  NevercBinaryOperatorInfo BinaryInfo{};
  BinaryInfo.Header.StructSize = sizeof(BinaryInfo);
  ASSERT_EQ(api()
                .GetBinaryOperatorInfo(api().Context, task().handle(), Binary,
                                       &BinaryInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(BinaryInfo.Operator.Data,
                        static_cast<size_t>(BinaryInfo.Operator.Length)),
            "+");
  EXPECT_FALSE(neverc_handle_is_null(BinaryInfo.Left));
  EXPECT_FALSE(neverc_handle_is_null(BinaryInfo.Right));
  EXPECT_FALSE(neverc_handle_is_null(BinaryInfo.Type));

  NevercFunctionDeclInfo UseFunction{};
  UseFunction.Header.StructSize = sizeof(UseFunction);
  ASSERT_EQ(api()
                .GetFunctionDeclInfo(api().Context, task().handle(), Use,
                                     &UseFunction)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTNodeHandle Call = findFirstNodeKind(
      api(), task().handle(), UseFunction.Body, NEVERC_STMT_KIND_CALL_EXPR);
  ASSERT_FALSE(neverc_handle_is_null(Call));
  NevercCallExprInfo CallInfo{};
  CallInfo.Header.StructSize = sizeof(CallInfo);
  ASSERT_EQ(
      api()
          .GetCallExprInfo(api().Context, task().handle(), Call, &CallInfo)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(CallInfo.ArgumentCount, 2U);
  EXPECT_TRUE(sameHandle(CallInfo.DirectCallee, Add));
  EXPECT_FALSE(neverc_handle_is_null(CallInfo.Callee));

  NevercExprHandle FirstArgument{};
  ASSERT_EQ(api()
                .GetCallExprArgument(api().Context, task().handle(), Call, 0,
                                     &FirstArgument)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTNodeHandle Reference = findFirstNodeKind(
      api(), task().handle(), FirstArgument, NEVERC_STMT_KIND_DECL_REF_EXPR);
  ASSERT_FALSE(neverc_handle_is_null(Reference));
  NevercDeclRefExprInfo ReferenceInfo{};
  ReferenceInfo.Header.StructSize = sizeof(ReferenceInfo);
  ASSERT_EQ(api()
                .GetDeclRefExprInfo(api().Context, task().handle(), Reference,
                                    &ReferenceInfo)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTValue ReferencedName{};
  ReferencedName.Header.StructSize = sizeof(ReferencedName);
  ASSERT_EQ(api()
                .GetProperty(api().Context, task().handle(),
                             ReferenceInfo.ReferencedDecl,
                             NEVERC_AST_PROPERTY_DECL_NAME, &ReferencedName)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ReferencedName.Type, NEVERC_AST_VALUE_IDENTIFIER);
  EXPECT_EQ(
      identifierName(prepAPI(), task().handle(), ReferencedName.NodeValue),
      "global");
}

} // namespace
} // namespace neverc::test
