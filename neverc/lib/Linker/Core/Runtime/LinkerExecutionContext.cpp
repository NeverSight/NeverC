#include "Linker/Core/Runtime/LinkerExecutionContext.h"

namespace linker {

LinkerExecutionContext::LinkerExecutionContext()
    : LinkerExecutionContext(neverc::ResourceSessionView{}) {}

LinkerExecutionContext::LinkerExecutionContext(
    neverc::ResourceSessionView ParentSession)
    : ResourcePermit(neverc::ProcessResourceBroker::global().acquireSession(
          std::move(ParentSession),
          neverc::ResourcePhase::LinkParseResolve)) {}

LinkerExecutionContext::~LinkerExecutionContext() { destroyBackend(); }

void LinkerExecutionContext::destroyBackend() { Backend.reset(); }

} // namespace linker
