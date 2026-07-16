#include "PluginFrontendTestSupport.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginSource.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"

namespace neverc::test {
using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

constexpr llvm::StringLiteral PrepTestSource = R"(
#define SUM(x, ...) x + __VA_OPT__(__VA_ARGS__)
SUM(value, 3)
)";

} // namespace

std::array<NevercInterfaceID, 5> frontendInterfaceIDs() {
  return {{
      {NEVERC_INTERFACE_IO_HIGH, NEVERC_INTERFACE_IO_LOW},
      {NEVERC_INTERFACE_SOURCE_LOCATION_HIGH,
       NEVERC_INTERFACE_SOURCE_LOCATION_LOW},
      {NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
      {NEVERC_INTERFACE_AST_HIGH, NEVERC_INTERFACE_AST_LOW},
      {NEVERC_INTERFACE_SEMA_HIGH, NEVERC_INTERFACE_SEMA_LOW},
  }};
}

PluginPrepTest::PluginPrepTest() = default;
PluginPrepTest::~PluginPrepTest() = default;

void PluginPrepTest::SetUp() {
  Services = std::make_unique<PluginProcessServices>(
      "neverc-plugin-prep-tests", LLVM_VERSION_MAJOR);
  ASSERT_FALSE(registerPluginFrontendInterface(*Services));
  ASSERT_FALSE(Services->interfaces().freeze());
  auto Loaded = Services->registry().load(NEVERC_TEST_MINIMAL_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded))
      << takeErrorMessage(Loaded.takeError());
  const std::array<StringRef, 1> Selected = {
      "org.neverc.test.minimal"};
  auto CreatedPlan =
      makePluginActivationPlan(Services->registry(), Selected);
  ASSERT_TRUE(static_cast<bool>(CreatedPlan))
      << takeErrorMessage(CreatedPlan.takeError());
  Plan = std::make_unique<PluginActivationPlan>(
      std::move(*CreatedPlan));
  auto CreatedSession = PluginSession::create(*Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(CreatedSession))
      << takeErrorMessage(CreatedSession.takeError());
  Session = std::move(*CreatedSession);
  auto CreatedTask =
      Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
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

  SourceManager &SourceMgr = Compiler->getSourceManager();
  FileID MainFile = SourceMgr.createFileID(
      MemoryBuffer::getMemBufferCopy(PrepTestSource, "prep-test.c"));
  ASSERT_TRUE(MainFile.isValid());
  SourceMgr.setMainFileID(MainFile);
  Compiler->getPrepEngine().InitMainInput();

  Locations = std::make_unique<FrontendPluginBridge>(
      *Task, SourceMgr, Compiler->getLangOpts());
  PrepBridge = std::make_unique<PluginPrepBridge>(
      *Task, Compiler->getPrepEngine(), *Locations);
  ASSERT_FALSE(PrepBridge->attachProcessInterface());
}

void PluginPrepTest::TearDown() {
  PrepBridge.reset();
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

PluginTaskContext &PluginPrepTest::task() { return *Task; }
PrepEngine &PluginPrepTest::prep() {
  return Compiler->getPrepEngine();
}
SourceManager &PluginPrepTest::sourceManager() {
  return Compiler->getSourceManager();
}
FrontendPluginBridge &PluginPrepTest::locations() {
  return *Locations;
}
PluginPrepBridge &PluginPrepTest::prepBridge() {
  return *PrepBridge;
}

std::vector<Token> PluginPrepTest::lexAll() {
  std::vector<Token> Tokens;
  do {
    Tokens.emplace_back();
    prep().Lex(Tokens.back());
  } while (!Tokens.back().is(tok::eof));
  return Tokens;
}

} // namespace neverc::test
