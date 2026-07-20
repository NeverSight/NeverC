#include "Linker/Core/Runtime/LinkerExecutionContext.h"

namespace linker {

LinkerExecutionContext::~LinkerExecutionContext() { destroyBackend(); }

void LinkerExecutionContext::destroyBackend() { Backend.reset(); }

} // namespace linker
