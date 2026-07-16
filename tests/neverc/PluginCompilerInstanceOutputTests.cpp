#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;

namespace {

std::string readOutput(StringRef Path) {
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return {};
  return (*Buffer)->getBuffer().str();
}

void writeOutput(StringRef Path, StringRef Contents) {
  std::error_code Error;
  raw_fd_ostream Output(Path, Error);
  ASSERT_FALSE(Error);
  Output << Contents;
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

TEST(PluginCompilerInstanceOutputTest,
     AbortingTemporaryOutputPreservesPreexistingPublication) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-compiler-output-abort", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  writeOutput(FinalPath, "existing");

  TextDiagnosticBuffer DiagnosticBuffer;
  CompilerInstance Compiler;
  Compiler.createDiagnostics(&DiagnosticBuffer,
                             /*ShouldOwnClient=*/false);
  auto Output = Compiler.createOutputFile(
      FinalPath, /*Binary=*/true, /*RemoveFileOnSignal=*/false,
      /*UseTemporary=*/true);
  ASSERT_NE(Output, nullptr);
  *Output << "replacement";
  Output.reset();

  Compiler.clearOutputFiles(/*EraseFiles=*/true);
  EXPECT_EQ(readOutput(FinalPath), "existing");
}

TEST(PluginCompilerInstanceOutputTest,
     CommitPublishesAtomicallyAndSupportsPwrite) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-compiler-output-commit", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  writeOutput(FinalPath, "existing");

  CompilerInstance Compiler;
  Compiler.createDiagnostics();
  auto Output = Compiler.createOutputFile(
      FinalPath, /*Binary=*/true, /*RemoveFileOnSignal=*/false,
      /*UseTemporary=*/false);
  ASSERT_NE(Output, nullptr);
  *Output << "abc";
  Output->pwrite("Z", 1, 1);
  EXPECT_EQ(readOutput(FinalPath), "existing");
  Output.reset();
  EXPECT_EQ(readOutput(FinalPath), "existing");

  Compiler.clearOutputFiles(/*EraseFiles=*/false);
  EXPECT_EQ(readOutput(FinalPath), "aZc");
}

TEST(PluginCompilerInstanceOutputTest,
     BuiltinAndPluginOutputsShareTheTaskPathLease) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-compiler-plugin-output-lease", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");

  PluginProcessServices Services{"neverc-compiler-output-tests",
                                 LLVM_VERSION_MAJOR};
  ASSERT_FALSE(registerPluginIOInterface(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
  ASSERT_TRUE(static_cast<bool>(CreatedPlan));
  std::optional<PluginActivationPlan> Plan(
      std::move(*CreatedPlan));
  auto CreatedSession = PluginSession::create(Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(CreatedSession));
  std::unique_ptr<PluginSession> Session = std::move(*CreatedSession);
  auto CreatedTask =
      Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(CreatedTask));
  std::unique_ptr<PluginTaskContext> Task = std::move(*CreatedTask);

  auto Query = Services.interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  ASSERT_TRUE(static_cast<bool>(Query));
  const auto *API = static_cast<const NevercIOAPI *>(Query->Table);
  ASSERT_NE(API, nullptr);

  NevercOutputSinkHandle PluginSink{};
  ASSERT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(FinalPath),
                UINT64_C(1024), &PluginSink)
                .Code,
            NEVERC_STATUS_OK);

  TextDiagnosticBuffer DiagnosticBuffer;
  CompilerInstance Compiler;
  Compiler.createDiagnostics(&DiagnosticBuffer,
                             /*ShouldOwnClient=*/false);
  Compiler.setPluginTaskContext(std::move(Task));
  auto BuiltinOutput = Compiler.createOutputFile(
      FinalPath, /*Binary=*/true, /*RemoveFileOnSignal=*/false,
      /*UseTemporary=*/true);
  EXPECT_EQ(BuiltinOutput, nullptr);

  PluginTaskContext *CompilerTask = Compiler.getPluginTaskContext();
  ASSERT_NE(CompilerTask, nullptr);
  EXPECT_EQ(API->OutputAbort(API->Context, CompilerTask->handle(),
                             PluginSink)
                .Code,
            NEVERC_STATUS_OK);
  std::unique_ptr<PluginTaskContext> ReturnedTask =
      Compiler.takePluginTaskContext();
  EXPECT_FALSE(ReturnedTask->end());
  ReturnedTask.reset();
  EXPECT_FALSE(Session->end());
  Session.reset();
  Plan.reset();
  EXPECT_FALSE(Services.shutdown());
}

} // namespace
