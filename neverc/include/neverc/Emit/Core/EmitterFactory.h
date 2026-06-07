#ifndef NEVERC_EMIT_CORE_EMITTERFACTORY_H
#define NEVERC_EMIT_CORE_EMITTERFACTORY_H

#include <memory>

namespace neverc {

class FrontendAction;

std::unique_ptr<FrontendAction> CreateEmitterAction(unsigned ActionKind);

} // namespace neverc

#endif
