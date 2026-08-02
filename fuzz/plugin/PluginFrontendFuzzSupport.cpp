#include "PluginFrontendFuzzSupport.h"
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
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <utility>

namespace {
void ensureCommandLineRttiLinked() {
    /*
     * Plugin fuzzers link with --gc-sections. Without a root reference,
     * support_cpp.cpp's GenericOptionValue::anchor is discarded and
     * OptionValueCopy<unsigned int> fails to link its base typeinfo.
     */
    (void)&llvm::cl::GenericOptionValue::anchor;
}
} // namespace

namespace neverc::fuzz {
using namespace llvm;
using namespace neverc::plugin;

uint8_t ByteCursor::takeByte() {
  if (empty())
    return 0;
  return Data[Offset++];
}

uint32_t ByteCursor::takeU32() {
  uint32_t Value = 0;
  for (unsigned Shift = 0; Shift != 32 && !empty(); Shift += 8)
    Value |= static_cast<uint32_t>(takeByte()) << Shift;
  return Value;
}

uint64_t ByteCursor::takeU64() {
  uint64_t Value = 0;
  for (unsigned Shift = 0; Shift != 64 && !empty(); Shift += 8)
    Value |= static_cast<uint64_t>(takeByte()) << Shift;
  return Value;
}

ArrayRef<uint8_t> ByteCursor::takeBytes(size_t Maximum) {
  const size_t Count = std::min(Maximum, remaining());
  const uint8_t *Begin = Count == 0 ? nullptr : Data + Offset;
  ArrayRef<uint8_t> Result(Begin, Count);
  Offset += Count;
  return Result;
}

PluginFuzzRuntime::PluginFuzzRuntime() = default;

Expected<std::unique_ptr<PluginFuzzRuntime>> PluginFuzzRuntime::create() {
  ensureCommandLineRttiLinked();
  auto Runtime =
      std::unique_ptr<PluginFuzzRuntime>(new PluginFuzzRuntime());
  Runtime->Services = std::make_unique<PluginProcessServices>(
      "neverc-plugin-fuzzers", LLVM_VERSION_MAJOR);
  if (Error E = registerPluginFrontendInterface(*Runtime->Services))
    return std::move(E);
  if (Error E = Runtime->Services->interfaces().freeze())
    return std::move(E);

  auto CreatedPlan = makePluginActivationPlan(
      Runtime->Services->registry(), ArrayRef<StringRef>());
  if (!CreatedPlan)
    return CreatedPlan.takeError();
  Runtime->Plan =
      std::make_unique<PluginActivationPlan>(std::move(*CreatedPlan));

  auto CreatedSession =
      PluginSession::create(*Runtime->Services, *Runtime->Plan);
  if (!CreatedSession)
    return CreatedSession.takeError();
  Runtime->Session = std::move(*CreatedSession);
  return std::move(Runtime);
}

PluginFuzzRuntime::~PluginFuzzRuntime() {
  if (Session && !Session->isEnded())
    consumeError(Session->end());
  Session.reset();
  Plan.reset();
  if (Services)
    consumeError(Services->shutdown());
}

PluginSession &PluginFuzzRuntime::session() const { return *Session; }

PluginFuzzRuntime &pluginFuzzRuntime() {
  static std::unique_ptr<PluginFuzzRuntime> Runtime = [] {
    auto Created = PluginFuzzRuntime::create();
    if (!Created) {
      auto Message = toString(Created.takeError());
      llvm::report_fatal_error(llvm::StringRef(Message));
    }
    return std::move(*Created);
  }();
  return *Runtime;
}

PluginFrontendFuzzIteration::PluginFrontendFuzzIteration(
    PluginFuzzRuntime &RuntimeValue)
    : Runtime(RuntimeValue) {}

Expected<std::unique_ptr<PluginFrontendFuzzIteration>>
PluginFrontendFuzzIteration::create(PluginFuzzRuntime &Runtime,
                                    bool ParseAST) {
  auto Iteration = std::unique_ptr<PluginFrontendFuzzIteration>(
      new PluginFrontendFuzzIteration(Runtime));
  auto CreatedTask =
      Runtime.session().createTask(NEVERC_TASK_TRANSLATION_UNIT);
  if (!CreatedTask)
    return CreatedTask.takeError();
  Iteration->Task = std::move(*CreatedTask);

  Iteration->Compiler = std::make_unique<CompilerInstance>();
  Iteration->Compiler->getInvocation().getTargetOpts().Triple =
      sys::getDefaultTargetTriple();
  Iteration->Compiler->createDiagnostics();
  if (!Iteration->Compiler->createFileManager())
    return createStringError(inconvertibleErrorCode(),
                             "fuzzer could not create a file manager");
  Iteration->Compiler->createSourceManager(
      Iteration->Compiler->getFileManager());
  if (!Iteration->Compiler->createTarget())
    return createStringError(inconvertibleErrorCode(),
                             "fuzzer could not create target information");
  Iteration->Compiler->createPrepEngine();

  static constexpr char Source[] = R"(
static int fuzz_global = 7;
int fuzz_function(int value) { return fuzz_global + value; }
)";
  SourceManager &SourceMgr = Iteration->Compiler->getSourceManager();
  FileID MainFile = SourceMgr.createFileID(
      MemoryBuffer::getMemBufferCopy(Source, "plugin-fuzzer.c"));
  if (MainFile.isInvalid())
    return createStringError(inconvertibleErrorCode(),
                             "fuzzer could not create the main file");
  SourceMgr.setMainFileID(MainFile);

  if (ParseAST) {
    Iteration->Compiler->createTreeContext();
    Iteration->Compiler->setTreeConsumer(std::make_unique<TreeConsumer>());
    Iteration->Compiler->createSema();
    RunParser(Iteration->Compiler->getSema());
  } else {
    Iteration->Compiler->getPrepEngine().InitMainInput();
  }

  Iteration->Locations = std::make_unique<FrontendPluginBridge>(
      *Iteration->Task, SourceMgr, Iteration->Compiler->getLangOpts());
  Iteration->Prep = std::make_unique<PluginPrepBridge>(
      *Iteration->Task, Iteration->Compiler->getPrepEngine(),
      *Iteration->Locations);
  if (Error E = Iteration->Prep->attachProcessInterface())
    return std::move(E);

  if (ParseAST) {
    Iteration->AST = std::make_unique<PluginASTBridge>(
        *Iteration->Task, Iteration->Compiler->getTreeContext(),
        *Iteration->Locations, Iteration->Prep.get());
    if (Error E = Iteration->AST->attachProcessInterface())
      return std::move(E);
  }
  return std::move(Iteration);
}

PluginFrontendFuzzIteration::~PluginFrontendFuzzIteration() {
  AST.reset();
  Prep.reset();
  Locations.reset();
  Compiler.reset();
  if (Task && !Task->isEnded())
    consumeError(Task->end());
}

PluginTaskContext &PluginFrontendFuzzIteration::task() const { return *Task; }

FrontendPluginBridge &PluginFrontendFuzzIteration::locations() const {
  return *Locations;
}

PluginPrepBridge &PluginFrontendFuzzIteration::prepBridge() const {
  return *Prep;
}

const NevercPrepAPI &PluginFrontendFuzzIteration::prepAPI() const {
  return Prep->prepAPI();
}

PluginASTBridge *PluginFrontendFuzzIteration::astBridge() const {
  return AST.get();
}

const NevercASTAPI *PluginFrontendFuzzIteration::astAPI() const {
  return AST ? &AST->astAPI() : nullptr;
}

Expected<NevercSourceLocation>
PluginFrontendFuzzIteration::anchorLocation() const {
  SourceManager &SourceMgr = Compiler->getSourceManager();
  FileID MainFile = SourceMgr.getMainFileID();
  if (MainFile.isInvalid())
    return createStringError(inconvertibleErrorCode(),
                             "fuzzer main file is unavailable");
  return Locations->createLocation(
      SourceMgr.getLocForStartOfFile(MainFile));
}

PrepEngine &PluginFrontendFuzzIteration::prepEngine() const {
  return Compiler->getPrepEngine();
}

std::vector<Token> PluginFrontendFuzzIteration::lexAllTokens() {
  std::vector<Token> Tokens;
  do {
    Tokens.emplace_back();
    prepEngine().Lex(Tokens.back());
  } while (!Tokens.back().is(tok::eof));
  return Tokens;
}

NevercHandle arbitraryHandle(ByteCursor &Input) {
  return {Input.takeU64(), Input.takeU64()};
}

NevercTaskHandle chooseTaskHandle(ByteCursor &Input,
                                 NevercTaskHandle ValidTask) {
  switch (Input.takeByte() & 3U) {
  case 0:
    return ValidTask;
  case 1:
    return {};
  case 2:
    return arbitraryHandle(Input);
  default:
    ValidTask.Value ^= UINT64_C(0x100000001);
    return ValidTask;
  }
}

} // namespace neverc::fuzz
