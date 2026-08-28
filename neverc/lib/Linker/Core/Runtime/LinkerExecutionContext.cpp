#include "Linker/Core/Runtime/LinkerExecutionContext.h"

namespace linker {

LinkerExecutionContext::LinkerExecutionContext()
    : ResourcePermit(neverc::ProcessResourceBroker::global().acquireSession(
          neverc::ResourcePhase::LinkParseResolve)) {}

LinkerExecutionContext::~LinkerExecutionContext() { destroyBackend(); }

void LinkerExecutionContext::destroyBackend() { Backend.reset(); }

} // namespace linker
