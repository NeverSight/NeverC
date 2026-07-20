#ifndef LINKER_CORE_RUNTIME_LINKEREXECUTIONCONTEXT_H
#define LINKER_CORE_RUNTIME_LINKEREXECUTIONCONTEXT_H

#include "Linker/Core/Runtime/Session.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>
#include <type_traits>
#include <utility>

namespace linker {

/// Owns all runtime state for exactly one in-process link invocation.
class LinkerExecutionContext {
public:
  LinkerExecutionContext() = default;
  LinkerExecutionContext(const LinkerExecutionContext &) = delete;
  LinkerExecutionContext &
  operator=(const LinkerExecutionContext &) = delete;
  ~LinkerExecutionContext();

  template <typename BackendContext, typename... Args>
  BackendContext &createBackend(Args &&...Arguments) {
    static_assert(
        std::is_base_of_v<CommonLinkerContext, BackendContext>,
        "linker backend context must own CommonLinkerContext state");
    if (Backend)
      llvm::report_fatal_error(
          "LinkerExecutionContext already owns a backend");
    auto Created = std::make_unique<BackendContext>(
        std::forward<Args>(Arguments)...);
    BackendContext &Result = *Created;
    Backend = std::move(Created);
    return Result;
  }

  bool hasBackend() const { return static_cast<bool>(Backend); }
  CommonLinkerContext *common() const { return Backend.get(); }
  void destroyBackend();

private:
  std::unique_ptr<CommonLinkerContext> Backend;
};

} // namespace linker

#endif
