#include "neverc/Build/BuiltinCommands.h"
#include "Platform/Builtins/Internal.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <mutex>

namespace neverc {
namespace build {
namespace builtins {

namespace {

std::mutex &builtinIoMutex() {
  static std::mutex IoMutex;
  return IoMutex;
}

} // namespace

bool tryExecute(llvm::StringRef Command, int &ExitCode, bool EchoCommand,
                bool *Echoed) {
  if (Echoed)
    *Echoed = false;

  llvm::StringRef Trimmed = Command.trim();
  if (Trimmed.empty())
    return false;

  llvm::SmallVector<internal::Token, 8> Argv;
  if (!internal::tokenizeRecipe(Trimmed, Argv) || Argv.empty())
    return false;

  using Handler = bool (*)(llvm::ArrayRef<internal::Token>, int &);
  using namespace internal;

  // Same X-macro + StringSwitch pattern as BuiltinString::isMethodName.
  const Handler Fn =
      llvm::StringSwitch<Handler>(Argv[0].Text)
#define NEVERC_BUILTIN_COMMAND(Name, FnName) .Case(Name, FnName)
#include "Platform/Builtins/BuiltinCommands.def"
#undef NEVERC_BUILTIN_COMMAND
          .Default(nullptr);
  if (!Fn)
    return false;

  // Handlers return false only before producing I/O (unsupported form). Make
  // still echoes the recipe before running the host tool, so echo as soon as
  // we dispatch a known builtin name; the caller uses Echoed to avoid a
  // duplicate print when falling back.
  auto echoLocked = [&]() {
    if (!EchoCommand)
      return;
    llvm::outs() << Trimmed << '\n';
    llvm::outs().flush();
    if (Echoed)
      *Echoed = true;
  };

  // JobScheduler runs recipes under std::async (-j). Builtin handlers write to
  // llvm::outs()/errs() in-process, so serialize I/O across worker threads.
  // sleep is exempt from holding the mutex during the wait (would stall
  // unrelated jobs); still echo under the lock when requested.
  if (Fn == &tryExecuteSleep) {
    if (EchoCommand) {
      std::lock_guard<std::mutex> Lock(builtinIoMutex());
      echoLocked();
    }
    return Fn(Argv, ExitCode);
  }

  std::lock_guard<std::mutex> Lock(builtinIoMutex());
  echoLocked();
  return Fn(Argv, ExitCode);
}

} // namespace builtins
} // namespace build
} // namespace neverc
