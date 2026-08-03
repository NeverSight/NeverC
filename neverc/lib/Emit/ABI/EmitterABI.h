#ifndef NEVERC_LIB_EMIT_ABI_EMITTERABI_H
#define NEVERC_LIB_EMIT_ABI_EMITTERABI_H

#include "Core/FunctionEmitter.h"
#include <memory>

namespace neverc {
class MangleContext;

namespace Emit {
class FunctionEmitter;
class ModuleEmitter;
class CXXABI;

class CGABI {
  friend class ModuleEmitter;

  ModuleEmitter &ME;
  std::unique_ptr<MangleContext> MangleCtx;
  std::unique_ptr<CXXABI> TheCXXABI;

public:
  explicit CGABI(ModuleEmitter &ME);
  ~CGABI();

  MangleContext &getMangleContext() { return *MangleCtx; }

  /// NeverC-only C++ ABI v1 (owned; created when LangOpts.CPlusPlus).
  CXXABI *getCXXABI() { return TheCXXABI.get(); }
  const CXXABI *getCXXABI() const { return TheCXXABI.get(); }

  enum RecordArgABI { RAA_Default = 0, RAA_DirectInMemory, RAA_Indirect };
};

} // namespace Emit
} // namespace neverc

#endif // NEVERC_LIB_EMIT_ABI_EMITTERABI_H
