#ifndef NEVERC_FUZZ_PLUGIN_PLUGINFRONTENDFUZZSUPPORT_H
#define NEVERC_FUZZ_PLUGIN_PLUGINFRONTENDFUZZSUPPORT_H

#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace neverc {
class CompilerInstance;
class PrepEngine;

namespace plugin {
class FrontendPluginBridge;
class PluginActivationPlan;
class PluginASTBridge;
class PluginPrepBridge;
class PluginProcessServices;
class PluginSession;
class PluginTaskContext;
} // namespace plugin

namespace fuzz {

class ByteCursor {
public:
  ByteCursor(const uint8_t *Data, size_t Size) : Data(Data), Size(Size) {}

  bool empty() const { return Offset == Size; }
  size_t remaining() const { return Size - Offset; }
  uint8_t takeByte();
  uint32_t takeU32();
  uint64_t takeU64();
  llvm::ArrayRef<uint8_t> takeBytes(size_t Maximum);

private:
  const uint8_t *Data = nullptr;
  size_t Size = 0;
  size_t Offset = 0;
};

class PluginFuzzRuntime {
public:
  static llvm::Expected<std::unique_ptr<PluginFuzzRuntime>> create();
  ~PluginFuzzRuntime();

  PluginFuzzRuntime(const PluginFuzzRuntime &) = delete;
  PluginFuzzRuntime &operator=(const PluginFuzzRuntime &) = delete;

  plugin::PluginSession &session() const;

private:
  PluginFuzzRuntime();

  std::unique_ptr<plugin::PluginProcessServices> Services;
  std::unique_ptr<plugin::PluginActivationPlan> Plan;
  std::unique_ptr<plugin::PluginSession> Session;
};

PluginFuzzRuntime &pluginFuzzRuntime();

class PluginFrontendFuzzIteration {
public:
  static llvm::Expected<std::unique_ptr<PluginFrontendFuzzIteration>>
  create(PluginFuzzRuntime &Runtime, bool ParseAST);
  ~PluginFrontendFuzzIteration();

  PluginFrontendFuzzIteration(const PluginFrontendFuzzIteration &) = delete;
  PluginFrontendFuzzIteration &
  operator=(const PluginFrontendFuzzIteration &) = delete;

  plugin::PluginTaskContext &task() const;
  plugin::FrontendPluginBridge &locations() const;
  plugin::PluginPrepBridge &prepBridge() const;
  const NevercPrepAPI &prepAPI() const;
  plugin::PluginASTBridge *astBridge() const;
  const NevercASTAPI *astAPI() const;
  llvm::Expected<NevercSourceLocation> anchorLocation() const;
  PrepEngine &prepEngine() const;
  std::vector<Token> lexAllTokens();

private:
  explicit PluginFrontendFuzzIteration(PluginFuzzRuntime &Runtime);

  PluginFuzzRuntime &Runtime;
  std::unique_ptr<plugin::PluginTaskContext> Task;
  std::unique_ptr<CompilerInstance> Compiler;
  std::unique_ptr<plugin::FrontendPluginBridge> Locations;
  std::unique_ptr<plugin::PluginPrepBridge> Prep;
  std::unique_ptr<plugin::PluginASTBridge> AST;
};

NevercHandle arbitraryHandle(ByteCursor &Input);
NevercTaskHandle chooseTaskHandle(ByteCursor &Input,
                                 NevercTaskHandle ValidTask);

} // namespace fuzz
} // namespace neverc

#endif
