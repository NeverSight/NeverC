#ifndef NEVERC_LIB_PLUGIN_PYTHON_PYTHONPLUGINFFI_H
#define NEVERC_LIB_PLUGIN_PYTHON_PYTHONPLUGINFFI_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "neverc/Plugin/PluginCore.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace neverc::plugin {

inline constexpr const char *PythonContextCapsuleName =
    "neverc_plugin.context.v1";

struct PythonPluginRuntimeToken {
  std::atomic<void *> Runtime{nullptr};
  uint64_t Identity = 0;
  std::shared_ptr<void> FFIState;
};

enum class PythonContextKind : uint32_t {
  Registration = 1,
  Process = 2,
  Session = 3,
  Task = 4,
  Frame = 5,
  Callback = 6,
};

enum PythonCapability : uint64_t {
  PythonCapabilityCore = UINT64_C(1) << 0,
  PythonCapabilityRegistrar = UINT64_C(1) << 1,
  PythonCapabilityRegistrarContext = UINT64_C(1) << 2,
  PythonCapabilitySession = UINT64_C(1) << 3,
  PythonCapabilityTask = UINT64_C(1) << 4,
  PythonCapabilityFrame = UINT64_C(1) << 5,
  PythonCapabilityContinuation = UINT64_C(1) << 6,
  PythonCapabilityInvocation = UINT64_C(1) << 7,
};

struct PythonPluginNativeContext {
  PythonContextKind Kind = PythonContextKind::Process;
  bool Active = true;
  uint64_t Identity = 0;
  uint64_t CapabilityMask = 0;
  std::shared_ptr<PythonPluginRuntimeToken> Token;
  const NevercCoreAPI *Core = nullptr;
  const NevercRegistrarAPI *Registrar = nullptr;
  void *RegistrarContext = nullptr;
  NevercSessionHandle Session{};
  NevercTaskHandle Task{};
  const NevercPhaseFrame *Frame = nullptr;
  const NevercPhaseContinuation *Continuation = nullptr;
  const void *Invocation = nullptr;
  std::string PluginID;
  std::string PhaseName;
};

/// Return an active, authentic context or set a Python exception.
PythonPluginNativeContext *checkedPythonContext(PyObject *Object);
std::shared_ptr<PythonPluginRuntimeToken> makePythonRuntimeToken(void *Runtime);
PyObject *
makePythonContextCapsule(std::unique_ptr<PythonPluginNativeContext> Context,
                         PythonPluginNativeContext **OutContext = nullptr);

enum class PythonCallbackArgumentKind : uint8_t {
  Signed,
  Unsigned,
  Pointer,
  Record,
};

struct PythonCallbackArgument {
  PythonCallbackArgumentKind Kind = PythonCallbackArgumentKind::Unsigned;
  const char *TypeName = nullptr;
  int64_t SignedValue = 0;
  uint64_t UnsignedValue = 0;
  const void *PointerValue = nullptr;
  const void *RecordData = nullptr;
  size_t RecordSize = 0;
};

template <typename T>
PythonCallbackArgument makePythonCallbackArgument(const char *TypeName,
                                                  const T &Value) {
  PythonCallbackArgument Result;
  Result.TypeName = TypeName;
  if constexpr (std::is_pointer_v<T>) {
    Result.Kind = PythonCallbackArgumentKind::Pointer;
    Result.PointerValue = reinterpret_cast<const void *>(Value);
  } else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) {
    if constexpr (std::is_signed_v<T>) {
      Result.Kind = PythonCallbackArgumentKind::Signed;
      Result.SignedValue = static_cast<int64_t>(Value);
    } else {
      Result.Kind = PythonCallbackArgumentKind::Unsigned;
      Result.UnsignedValue = static_cast<uint64_t>(Value);
    }
  } else {
    static_assert(std::is_trivially_copyable_v<T>,
                  "NeverC callback values must be ABI-copyable");
    Result.Kind = PythonCallbackArgumentKind::Record;
    Result.RecordData = &Value;
    Result.RecordSize = sizeof(T);
  }
  return Result;
}

NevercStatus invokePythonStatusCallback(void *UserData, const char *Symbol,
                                        const PythonCallbackArgument *Arguments,
                                        size_t ArgumentCount);
void invokePythonVoidCallback(void *UserData, const char *Symbol,
                              const PythonCallbackArgument *Arguments,
                              size_t ArgumentCount);
void pythonCallbackUserDataDestroyed(void *UserData);
void destroyPythonRuntimeBindings(
    const std::shared_ptr<PythonPluginRuntimeToken> &Token);

/// Private `_neverc_plugin` methods consumed by `neverc_plugin.ffi`.
PyObject *pythonContextCapabilities(PyObject *, PyObject *Arguments);
PyObject *pythonContextIsActive(PyObject *, PyObject *Arguments);
PyObject *pythonBindCallbackRecord(PyObject *, PyObject *Arguments);
PyObject *pythonTransferCallbackBinding(PyObject *, PyObject *Arguments);
PyObject *pythonReleaseCallbackBinding(PyObject *, PyObject *Arguments);

} // namespace neverc::plugin

#endif
