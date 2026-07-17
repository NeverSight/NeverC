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
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <array>
#include <iterator>
#include <memory>
#include <string>

namespace neverc::test {
namespace {

using namespace llvm;
using namespace neverc::plugin;

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class PluginSemaAPITest : public testing::Test {
protected:
  void SetUp() override {
    Services = std::make_unique<PluginProcessServices>(
        "neverc-plugin-sema-api-tests", LLVM_VERSION_MAJOR);
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
    DiagnosticBuffer = std::make_unique<TextDiagnosticBuffer>();
    Compiler->createDiagnostics(DiagnosticBuffer.get(),
                                /*ShouldOwnClient=*/false);
    ASSERT_NE(Compiler->createFileManager(), nullptr);
    Compiler->createSourceManager(Compiler->getFileManager());
    ASSERT_TRUE(Compiler->createTarget());
    Compiler->createPrepEngine();
    Compiler->getPrepEngine().getBuiltinInfo().initializeBuiltins(
        Compiler->getPrepEngine().getIdentifierTable(),
        Compiler->getLangOpts());

    constexpr llvm::StringLiteral Source = R"(
struct Pair { int member; };
__attribute__((deprecated)) static int global_value = 7;
static const int values[2] = {3, 4};
int add(int lhs, int rhs) { return lhs + rhs; }
)";
    SourceManager &SourceMgr = Compiler->getSourceManager();
    FileID MainFile = SourceMgr.createFileID(
        MemoryBuffer::getMemBufferCopy(Source, "sema-api-test.c"));
    ASSERT_TRUE(MainFile.isValid());
    SourceMgr.setMainFileID(MainFile);
    Compiler->createTreeContext();
    Compiler->setTreeConsumer(std::make_unique<TreeConsumer>());
    Compiler->createSema();
    RunParser(Compiler->getSema());

    Locations = std::make_unique<FrontendPluginBridge>(
        *Task, SourceMgr, Compiler->getLangOpts());
    Prep = std::make_unique<PluginPrepBridge>(
        *Task, Compiler->getPrepEngine(), *Locations);
    ASSERT_FALSE(Prep->attachProcessInterface());
    AST = std::make_unique<PluginASTBridge>(
        *Task, Compiler->getTreeContext(), *Locations, Prep.get());
    ASSERT_FALSE(AST->attachProcessInterface());
    SemaBridge = std::make_unique<PluginSemaBridge>(
        *Task, Compiler->getSema(), *AST, *Locations);
    ASSERT_FALSE(SemaBridge->attachProcessInterface());

    const NevercInterfaceID SemaInterface{NEVERC_INTERFACE_SEMA_HIGH,
                                          NEVERC_INTERFACE_SEMA_LOW};
    auto QueriedSema = Services->interfaces().query(
        SemaInterface, NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(QueriedSema))
        << takeErrorMessage(QueriedSema.takeError());
    SemaAPI = static_cast<const NevercSemaAPI *>(QueriedSema->Table);
    ASSERT_NE(SemaAPI, nullptr);

    const NevercInterfaceID ASTInterface{NEVERC_INTERFACE_AST_HIGH,
                                         NEVERC_INTERFACE_AST_LOW};
    auto QueriedAST = Services->interfaces().query(
        ASTInterface, NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(QueriedAST))
        << takeErrorMessage(QueriedAST.takeError());
    ASTAPI = static_cast<const NevercASTAPI *>(QueriedAST->Table);
    ASSERT_NE(ASTAPI, nullptr);
  }

  void TearDown() override {
    SemaAPI = nullptr;
    ASTAPI = nullptr;
    SemaBridge.reset();
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
  PluginSession &session() { return *Session; }
  CompilerInstance &compiler() { return *Compiler; }
  const NevercSemaAPI &sema() const { return *SemaAPI; }
  const NevercASTAPI &ast() const { return *ASTAPI; }
  const TextDiagnosticBuffer &diagnostics() const { return *DiagnosticBuffer; }

  NevercDeclHandle lookupDeclaration(const std::string &Name,
                                     NevercSemaLookupKind Kind) {
    NevercSemaScopeHandle Scope{};
    if (sema()
            .GetCurrentScope(sema().Context, task().handle(), &Scope)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercSemaLookupRequest Request{};
    Request.Header = {sizeof(Request), NEVERC_SEMA_API_MAJOR,
                      NEVERC_SEMA_API_MINOR, 0};
    Request.Scope = Scope;
    Request.Name = {Name.data(), static_cast<uint64_t>(Name.size())};
    Request.Kind = Kind;
    Request.IncludeHidden = NEVERC_TRUE;
    NevercLookupResultHandle Result{};
    if (sema()
            .LookupName(sema().Context, task().handle(), &Request, &Result)
            .Code != NEVERC_STATUS_OK)
      return {};
    NevercDeclHandle Declaration{};
    sema().GetLookupCandidate(sema().Context, task().handle(), Result, 0,
                              &Declaration);
    sema().DestroyLookupResult(sema().Context, task().handle(), Result);
    return Declaration;
  }

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
  std::unique_ptr<PluginSemaBridge> SemaBridge;
  const NevercSemaAPI *SemaAPI = nullptr;
  const NevercASTAPI *ASTAPI = nullptr;
};

TEST_F(PluginSemaAPITest, EnumeratesScopesAndLooksUpDeclarations) {
  NevercSemaScopeHandle Scope{};
  ASSERT_EQ(sema().GetCurrentScope(sema().Context, task().handle(), &Scope).Code,
            NEVERC_STATUS_OK);

  NevercSemaScopeInfo ScopeInfo{};
  ScopeInfo.Header.StructSize = sizeof(ScopeInfo);
  ASSERT_EQ(sema()
                .GetScopeInfo(sema().Context, task().handle(), Scope, &ScopeInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(ScopeInfo.Flags & NEVERC_SEMA_SCOPE_FILE, 0U);
  EXPECT_GE(ScopeInfo.DeclarationCount, 3U);

  const std::string Name = "global_value";
  NevercSemaLookupRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
  Request.Scope = Scope;
  Request.Name = {Name.data(), static_cast<uint64_t>(Name.size())};
  Request.Kind = NEVERC_SEMA_LOOKUP_ORDINARY;
  Request.IncludeHidden = NEVERC_TRUE;
  NevercLookupResultHandle Result{};
  ASSERT_EQ(sema()
                .LookupName(sema().Context, task().handle(), &Request, &Result)
                .Code,
            NEVERC_STATUS_OK);

  NevercSemaLookupResultInfo ResultInfo{};
  ResultInfo.Header.StructSize = sizeof(ResultInfo);
  ASSERT_EQ(sema()
                .GetLookupResultInfo(sema().Context, task().handle(), Result,
                                     &ResultInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ResultInfo.Kind, NEVERC_SEMA_LOOKUP_FOUND);
  ASSERT_EQ(ResultInfo.CandidateCount, 1U);

  NevercDeclHandle Candidate{};
  ASSERT_EQ(sema()
                .GetLookupCandidate(sema().Context, task().handle(), Result, 0,
                                    &Candidate)
                .Code,
            NEVERC_STATUS_OK);
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(ast()
                .GetVarDeclInfo(ast().Context, task().handle(), Candidate,
                                &Variable)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(Variable.Name.Data,
                        static_cast<size_t>(Variable.Name.Length)),
            Name);
  EXPECT_EQ(sema()
                .DestroyLookupResult(sema().Context, task().handle(), Result)
                .Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginSemaAPITest, BuildsCanonicalCompositeTypesUnderMutationLease) {
  ASSERT_NE(sema().GetBuiltinType, nullptr);
  ASSERT_NE(sema().CreatePointerType, nullptr);
  ASSERT_NE(sema().CreateConstantArrayType, nullptr);
  ASSERT_NE(sema().CreateFunctionType, nullptr);
  ASSERT_NE(sema().CreateAtomicType, nullptr);
  ASSERT_NE(sema().CreateVectorType, nullptr);

  NevercTypeHandle IntType{};
  ASSERT_EQ(sema()
                .GetBuiltinType(sema().Context, task().handle(),
                                NEVERC_BUILTIN_TYPE_INT, &IntType)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaMutationLeaseHandle Lease{};
  ASSERT_EQ(sema()
                .AcquireMutationLease(sema().Context, task().handle(), &Lease)
                .Code,
            NEVERC_STATUS_OK);

  NevercTypeHandle PointerType{};
  ASSERT_EQ(sema()
                .CreatePointerType(sema().Context, task().handle(), Lease,
                                   IntType, &PointerType)
                .Code,
            NEVERC_STATUS_OK);
  NevercTypeInfo PointerInfo{};
  PointerInfo.Header.StructSize = sizeof(PointerInfo);
  ASSERT_EQ(ast()
                .GetTypeInfo(ast().Context, task().handle(), PointerType,
                             &PointerInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(PointerInfo.Flags & NEVERC_TYPE_FLAG_POINTER, 0U);

  NevercTypeHandle ArrayType{};
  ASSERT_EQ(sema()
                .CreateConstantArrayType(sema().Context, task().handle(), Lease,
                                         IntType, 4, &ArrayType)
                .Code,
            NEVERC_STATUS_OK);
  NevercTypeInfo ArrayInfo{};
  ArrayInfo.Header.StructSize = sizeof(ArrayInfo);
  ASSERT_EQ(ast()
                .GetTypeInfo(ast().Context, task().handle(), ArrayType,
                             &ArrayInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(ArrayInfo.Flags & NEVERC_TYPE_FLAG_ARRAY, 0U);
  EXPECT_EQ(ArrayInfo.ElementCount, 4U);

  const std::array<NevercTypeHandle, 2> Parameters = {IntType, PointerType};
  NevercSemaFunctionTypeDescriptor Function{};
  Function.Header = {sizeof(Function), NEVERC_SEMA_API_MAJOR,
                     NEVERC_SEMA_API_MINOR, 0};
  Function.ResultType = IntType;
  Function.ParameterTypes = Parameters.data();
  Function.ParameterCount = Parameters.size();
  Function.Variadic = NEVERC_FALSE;
  NevercTypeHandle FunctionType{};
  ASSERT_EQ(sema()
                .CreateFunctionType(sema().Context, task().handle(), Lease,
                                    &Function, &FunctionType)
                .Code,
            NEVERC_STATUS_OK);
  NevercTypeInfo FunctionInfo{};
  FunctionInfo.Header.StructSize = sizeof(FunctionInfo);
  ASSERT_EQ(ast()
                .GetTypeInfo(ast().Context, task().handle(), FunctionType,
                             &FunctionInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(FunctionInfo.Flags & NEVERC_TYPE_FLAG_FUNCTION, 0U);
  EXPECT_EQ(FunctionInfo.ElementCount, Parameters.size());

  NevercTypeHandle AtomicType{};
  ASSERT_EQ(sema()
                .CreateAtomicType(sema().Context, task().handle(), Lease,
                                  IntType, &AtomicType)
                .Code,
            NEVERC_STATUS_OK);
  NevercTypeHandle VectorType{};
  ASSERT_EQ(sema()
                .CreateVectorType(sema().Context, task().handle(), Lease,
                                  IntType, 4, NEVERC_SEMA_VECTOR_GENERIC,
                                  &VectorType)
                .Code,
            NEVERC_STATUS_OK);

  NevercTypeHandle CanonicalInt{};
  ASSERT_EQ(sema()
                .GetCanonicalType(sema().Context, task().handle(), IntType,
                                  &CanonicalInt)
                .Code,
            NEVERC_STATUS_OK);
  NevercBool Compatible = NEVERC_FALSE;
  ASSERT_EQ(sema()
                .AreTypesCompatible(sema().Context, task().handle(), IntType,
                                    CanonicalInt, &Compatible)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Compatible, NEVERC_TRUE);
  EXPECT_EQ(sema()
                .ReleaseMutationLease(sema().Context, task().handle(), Lease)
                .Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginSemaAPITest, ClassifiesAndAppliesConversions) {
  ASSERT_NE(sema().ClassifyImplicitConversion, nullptr);
  ASSERT_NE(sema().GetConversionSequenceInfo, nullptr);
  ASSERT_NE(sema().ApplyImplicitConversion, nullptr);
  ASSERT_NE(sema().CreateExplicitCast, nullptr);
  ASSERT_NE(sema().DestroyConversionSequence, nullptr);

  NevercTypeHandle IntType{};
  NevercTypeHandle DoubleType{};
  ASSERT_EQ(sema()
                .GetBuiltinType(sema().Context, task().handle(),
                                NEVERC_BUILTIN_TYPE_INT, &IntType)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(sema()
                .GetBuiltinType(sema().Context, task().handle(),
                                NEVERC_BUILTIN_TYPE_DOUBLE, &DoubleType)
                .Code,
            NEVERC_STATUS_OK);

  NevercConversionSequenceHandle Sequence{};
  ASSERT_EQ(sema()
                .ClassifyImplicitConversion(sema().Context, task().handle(),
                                            IntType, DoubleType, &Sequence)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaConversionSequenceInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  ASSERT_EQ(sema()
                .GetConversionSequenceInfo(sema().Context, task().handle(),
                                           Sequence, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Kind, NEVERC_SEMA_CONVERSION_COMPATIBLE);
  EXPECT_EQ(Info.Viable, NEVERC_TRUE);
  EXPECT_EQ(Info.RequiresDiagnostic, NEVERC_FALSE);

  NevercDeclHandle Global = lookupDeclaration(
      "global_value", NEVERC_SEMA_LOOKUP_ORDINARY);
  ASSERT_FALSE(neverc_handle_is_null(Global));
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(ast()
                .GetVarDeclInfo(ast().Context, task().handle(), Global,
                                &Variable)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Variable.Initializer));

  NevercSemaMutationLeaseHandle Lease{};
  ASSERT_EQ(sema()
                .AcquireMutationLease(sema().Context, task().handle(), &Lease)
                .Code,
            NEVERC_STATUS_OK);
  NevercExprHandle Converted{};
  ASSERT_EQ(sema()
                .ApplyImplicitConversion(
                    sema().Context, task().handle(), Lease, Sequence,
                    Variable.Initializer, NEVERC_SEMA_CONVERSION_INITIALIZATION,
                    &Converted)
                .Code,
            NEVERC_STATUS_OK);
  NevercASTValue ConvertedType{};
  ConvertedType.Header.StructSize = sizeof(ConvertedType);
  ASSERT_EQ(ast()
                .GetProperty(ast().Context, task().handle(), Converted,
                             NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &ConvertedType)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(ConvertedType.Type, NEVERC_AST_VALUE_TYPE);
  NevercBool Compatible = NEVERC_FALSE;
  ASSERT_EQ(sema()
                .AreTypesCompatible(sema().Context, task().handle(),
                                    ConvertedType.NodeValue, DoubleType,
                                    &Compatible)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Compatible, NEVERC_TRUE);

  NevercExprHandle Explicit{};
  ASSERT_EQ(sema()
                .CreateExplicitCast(sema().Context, task().handle(), Lease,
                                    Variable.Initializer, DoubleType, &Explicit)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(Explicit));
  EXPECT_EQ(sema()
                .ReleaseMutationLease(sema().Context, task().handle(), Lease)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(sema()
                .DestroyConversionSequence(sema().Context, task().handle(),
                                           Sequence)
                .Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginSemaAPITest, EvaluatesScalarConstants) {
  ASSERT_NE(sema().EvaluateConstant, nullptr);
  ASSERT_NE(sema().GetConstantValueInfo, nullptr);
  ASSERT_NE(sema().GetConstantIntegerWord, nullptr);
  ASSERT_NE(sema().GetConstantElement, nullptr);
  ASSERT_NE(sema().DestroyConstantValue, nullptr);

  NevercDeclHandle Global = lookupDeclaration(
      "global_value", NEVERC_SEMA_LOOKUP_ORDINARY);
  ASSERT_FALSE(neverc_handle_is_null(Global));
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(ast()
                .GetVarDeclInfo(ast().Context, task().handle(), Global,
                                &Variable)
                .Code,
            NEVERC_STATUS_OK);

  NevercConstantValueHandle Value{};
  ASSERT_EQ(sema()
                .EvaluateConstant(sema().Context, task().handle(),
                                  Variable.Initializer, &Value)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaConstantValueInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  ASSERT_EQ(sema()
                .GetConstantValueInfo(sema().Context, task().handle(), Value,
                                      &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Kind, NEVERC_SEMA_CONSTANT_INTEGER);
  EXPECT_EQ(Info.IsSigned, NEVERC_TRUE);
  EXPECT_EQ(Info.BitWidth, 32U);
  EXPECT_EQ(std::string(Info.Text.Data,
                        static_cast<size_t>(Info.Text.Length)),
            "7");
  uint64_t Word = 0;
  ASSERT_EQ(sema()
                .GetConstantIntegerWord(sema().Context, task().handle(), Value,
                                        0, &Word)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Word, 7U);
  EXPECT_EQ(sema()
                .DestroyConstantValue(sema().Context, task().handle(), Value)
                .Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginSemaAPITest, EnumeratesAggregateConstantElements) {
  NevercDeclHandle Values = lookupDeclaration(
      "values", NEVERC_SEMA_LOOKUP_ORDINARY);
  ASSERT_FALSE(neverc_handle_is_null(Values));
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  ASSERT_EQ(ast()
                .GetVarDeclInfo(ast().Context, task().handle(), Values,
                                &Variable)
                .Code,
            NEVERC_STATUS_OK);
  NevercConstantValueHandle Aggregate{};
  ASSERT_EQ(sema()
                .EvaluateConstant(sema().Context, task().handle(),
                                  Variable.Initializer, &Aggregate)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaConstantValueInfo AggregateInfo{};
  AggregateInfo.Header.StructSize = sizeof(AggregateInfo);
  ASSERT_EQ(sema()
                .GetConstantValueInfo(sema().Context, task().handle(),
                                      Aggregate, &AggregateInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(AggregateInfo.Kind, NEVERC_SEMA_CONSTANT_ARRAY);
  ASSERT_EQ(AggregateInfo.ElementCount, 2U);

  NevercConstantValueHandle Element{};
  ASSERT_EQ(sema()
                .GetConstantElement(sema().Context, task().handle(), Aggregate,
                                    1, &Element)
                .Code,
            NEVERC_STATUS_OK);
  uint64_t Word = 0;
  ASSERT_EQ(sema()
                .GetConstantIntegerWord(sema().Context, task().handle(),
                                        Element, 0, &Word)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Word, 4U);
  EXPECT_EQ(sema()
                .DestroyConstantValue(sema().Context, task().handle(), Element)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(sema()
                .DestroyConstantValue(sema().Context, task().handle(),
                                      Aggregate)
                .Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginSemaAPITest, QueriesAttributesAndBuiltinMetadata) {
  NevercDeclHandle Global = lookupDeclaration(
      "global_value", NEVERC_SEMA_LOOKUP_ORDINARY);
  ASSERT_FALSE(neverc_handle_is_null(Global));
  const std::string AttributeName = "deprecated";
  NevercBool Present = NEVERC_FALSE;
  ASSERT_EQ(sema()
                .HasDeclAttribute(
                    sema().Context, task().handle(), Global,
                    {AttributeName.data(),
                     static_cast<uint64_t>(AttributeName.size())},
                    &Present)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Present, NEVERC_TRUE);

  const std::string BuiltinName = "__builtin_expect";
  NevercSemaBuiltinInfo Builtin{};
  Builtin.Header.StructSize = sizeof(Builtin);
  ASSERT_EQ(sema()
                .GetBuiltinInfo(
                    sema().Context, task().handle(),
                    {BuiltinName.data(),
                     static_cast<uint64_t>(BuiltinName.size())},
                    &Builtin)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(Builtin.BuiltinID, 0U);
  EXPECT_EQ(std::string(Builtin.Name.Data,
                        static_cast<size_t>(Builtin.Name.Length)),
            BuiltinName);
}

TEST_F(PluginSemaAPITest, EmitsDiagnosticsUnderMutationLease) {
  const auto WarningsBefore =
      std::distance(diagnostics().warn_begin(), diagnostics().warn_end());
  const std::string Message = "plugin warning";
  NevercSemaMutationLeaseHandle Lease{};
  ASSERT_EQ(sema()
                .AcquireMutationLease(sema().Context, task().handle(), &Lease)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaDiagnosticDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_SEMA_API_MAJOR,
                       NEVERC_SEMA_API_MINOR, 0};
  Descriptor.Level = NEVERC_SEMA_DIAGNOSTIC_WARNING;
  Descriptor.Message = {Message.data(),
                        static_cast<uint64_t>(Message.size())};
  ASSERT_EQ(sema()
                .EmitDiagnostic(sema().Context, task().handle(), Lease,
                                &Descriptor)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(sema()
                .ReleaseMutationLease(sema().Context, task().handle(), Lease)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      std::distance(diagnostics().warn_begin(), diagnostics().warn_end()),
      WarningsBefore + 1);
  EXPECT_NE(std::prev(diagnostics().warn_end())->second.find(Message),
            std::string::npos);
}

TEST_F(PluginSemaAPITest, RoutesScopesToTheirOwningTask) {
  auto CreatedTask = session().createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(CreatedTask))
      << takeErrorMessage(CreatedTask.takeError());
  auto OtherTask = std::move(*CreatedTask);

  auto OtherCompiler = std::make_unique<CompilerInstance>();
  OtherCompiler->getInvocation().getTargetOpts().Triple =
      sys::getDefaultTargetTriple();
  OtherCompiler->createDiagnostics();
  ASSERT_NE(OtherCompiler->createFileManager(), nullptr);
  OtherCompiler->createSourceManager(OtherCompiler->getFileManager());
  ASSERT_TRUE(OtherCompiler->createTarget());
  OtherCompiler->createPrepEngine();
  OtherCompiler->getPrepEngine().getBuiltinInfo().initializeBuiltins(
      OtherCompiler->getPrepEngine().getIdentifierTable(),
      OtherCompiler->getLangOpts());
  constexpr llvm::StringLiteral OtherSource = "int other_task_only = 9;";
  SourceManager &OtherSourceMgr = OtherCompiler->getSourceManager();
  FileID OtherMainFile = OtherSourceMgr.createFileID(
      MemoryBuffer::getMemBufferCopy(OtherSource, "other-sema-task.c"));
  ASSERT_TRUE(OtherMainFile.isValid());
  OtherSourceMgr.setMainFileID(OtherMainFile);
  OtherCompiler->createTreeContext();
  OtherCompiler->setTreeConsumer(std::make_unique<TreeConsumer>());
  OtherCompiler->createSema();
  RunParser(OtherCompiler->getSema());

  auto OtherLocations = std::make_unique<FrontendPluginBridge>(
      *OtherTask, OtherSourceMgr, OtherCompiler->getLangOpts());
  auto OtherPrep = std::make_unique<PluginPrepBridge>(
      *OtherTask, OtherCompiler->getPrepEngine(), *OtherLocations);
  ASSERT_FALSE(OtherPrep->attachProcessInterface());
  auto OtherAST = std::make_unique<PluginASTBridge>(
      *OtherTask, OtherCompiler->getTreeContext(), *OtherLocations,
      OtherPrep.get());
  ASSERT_FALSE(OtherAST->attachProcessInterface());
  auto OtherSema = std::make_unique<PluginSemaBridge>(
      *OtherTask, OtherCompiler->getSema(), *OtherAST, *OtherLocations);
  ASSERT_FALSE(OtherSema->attachProcessInterface());

  NevercSemaScopeHandle FirstScope{};
  NevercSemaScopeHandle OtherScope{};
  ASSERT_EQ(sema()
                .GetCurrentScope(sema().Context, task().handle(), &FirstScope)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(sema()
                .GetCurrentScope(sema().Context, OtherTask->handle(),
                                 &OtherScope)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(FirstScope.Owner, OtherScope.Owner);

  NevercSemaScopeInfo WrongScopeInfo{};
  WrongScopeInfo.Header.StructSize = sizeof(WrongScopeInfo);
  EXPECT_EQ(sema()
                .GetScopeInfo(sema().Context, task().handle(), OtherScope,
                              &WrongScopeInfo)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);

  const std::string Name = "other_task_only";
  NevercSemaLookupRequest Request{};
  Request.Header = {sizeof(Request), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
  Request.Scope = OtherScope;
  Request.Name = {Name.data(), static_cast<uint64_t>(Name.size())};
  Request.Kind = NEVERC_SEMA_LOOKUP_ORDINARY;
  Request.IncludeHidden = NEVERC_TRUE;
  NevercLookupResultHandle Result{};
  ASSERT_EQ(sema()
                .LookupName(sema().Context, OtherTask->handle(), &Request,
                            &Result)
                .Code,
            NEVERC_STATUS_OK);
  NevercSemaLookupResultInfo ResultInfo{};
  ResultInfo.Header.StructSize = sizeof(ResultInfo);
  ASSERT_EQ(sema()
                .GetLookupResultInfo(sema().Context, OtherTask->handle(),
                                     Result, &ResultInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ResultInfo.Kind, NEVERC_SEMA_LOOKUP_FOUND);
  EXPECT_EQ(ResultInfo.CandidateCount, 1U);
  EXPECT_EQ(sema()
                .DestroyLookupResult(sema().Context, OtherTask->handle(),
                                     Result)
                .Code,
            NEVERC_STATUS_OK);

  OtherSema.reset();
  OtherAST.reset();
  OtherPrep.reset();
  OtherLocations.reset();
  OtherCompiler.reset();
  EXPECT_FALSE(OtherTask->end());
}

} // namespace
} // namespace neverc::test
