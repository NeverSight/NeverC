#ifndef NEVERC_LIB_EMIT_ABI_EMITTERABI_H
#define NEVERC_LIB_EMIT_ABI_EMITTERABI_H

#include "Core/FunctionEmitter.h"

namespace neverc {
class MangleContext;

namespace Emit {
class FunctionEmitter;
class ModuleEmitter;

class CGABI {
  friend class ModuleEmitter;

  ModuleEmitter &ME;
  std::unique_ptr<MangleContext> MangleCtx;

public:
  explicit CGABI(ModuleEmitter &ME);
  ~CGABI();

  MangleContext &getMangleContext() { return *MangleCtx; }

  enum RecordArgABI { RAA_Default = 0, RAA_DirectInMemory, RAA_Indirect };
};

} // namespace Emit
} // namespace neverc

#endif
