#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "PythonPluginFFI.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginLTO.h"
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginMIR.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSource.h"
#include "neverc/Plugin/PluginTarget.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neverc::plugin {

namespace {

class GILGuard {
public:
  GILGuard() : State(PyGILState_Ensure()) {}
  ~GILGuard() { PyGILState_Release(State); }

  GILGuard(const GILGuard &) = delete;
  GILGuard &operator=(const GILGuard &) = delete;

private:
  PyGILState_STATE State;
};

class PyRef {
public:
  PyRef() = default;
  explicit PyRef(PyObject *ObjectValue) : Object(ObjectValue) {}
  ~PyRef() { Py_XDECREF(Object); }

  PyRef(const PyRef &) = delete;
  PyRef &operator=(const PyRef &) = delete;
  PyRef(PyRef &&Other) noexcept : Object(Other.release()) {}
  PyRef &operator=(PyRef &&Other) noexcept {
    if (this != &Other) {
      Py_XDECREF(Object);
      Object = Other.release();
    }
    return *this;
  }

  PyObject *get() const { return Object; }
  explicit operator bool() const { return Object != nullptr; }
  PyObject *release() {
    PyObject *Result = Object;
    Object = nullptr;
    return Result;
  }

private:
  PyObject *Object = nullptr;
};

NevercStatus makeStatus(NevercStatusCode Code, uint32_t Flags = 0,
                        uint64_t Detail = 0) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  Result.Flags = Flags;
  Result.Detail = Detail;
  return Result;
}

NevercStringView stringView(const std::string &Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

std::string fallbackPythonException(PyObject *Value) {
  if (!Value)
    return "unknown Python exception";
  PyRef Text(PyObject_Str(Value));
  if (!Text) {
    PyErr_Clear();
    return "unprintable Python exception";
  }
  Py_ssize_t Length = 0;
  const char *Data = PyUnicode_AsUTF8AndSize(Text.get(), &Length);
  if (!Data) {
    PyErr_Clear();
    return "unprintable Python exception";
  }
  return std::string(Data, static_cast<size_t>(Length));
}

std::string formatPythonException() {
  if (!PyErr_Occurred())
    return "unknown Python exception";
  PyObject *RawType = nullptr;
  PyObject *RawValue = nullptr;
  PyObject *RawTraceback = nullptr;
  PyErr_Fetch(&RawType, &RawValue, &RawTraceback);
  PyErr_NormalizeException(&RawType, &RawValue, &RawTraceback);
  PyRef Type(RawType);
  PyRef Value(RawValue);
  PyRef Traceback(RawTraceback);
  PyRef Module(PyImport_ImportModule("traceback"));
  PyRef Formatter(Module
                      ? PyObject_GetAttrString(Module.get(), "format_exception")
                      : nullptr);
  PyObject *TracebackArgument = Traceback ? Traceback.get() : Py_None;
  PyRef Lines(Formatter ? PyObject_CallFunctionObjArgs(
                              Formatter.get(), Type ? Type.get() : Py_None,
                              Value ? Value.get() : Py_None, TracebackArgument,
                              nullptr)
                        : nullptr);
  PyRef Separator(PyUnicode_FromString(""));
  PyRef Joined(Separator && Lines ? PyUnicode_Join(Separator.get(), Lines.get())
                                  : nullptr);
  if (!Joined) {
    PyErr_Clear();
    return fallbackPythonException(Value.get());
  }
  Py_ssize_t Length = 0;
  const char *Data = PyUnicode_AsUTF8AndSize(Joined.get(), &Length);
  if (!Data) {
    PyErr_Clear();
    return fallbackPythonException(Value.get());
  }
  return std::string(Data, static_cast<size_t>(Length));
}

bool setOwned(PyObject *Dictionary, const char *Name, PyObject *Value) {
  if (!Value)
    return false;
  int Result = PyDict_SetItemString(Dictionary, Name, Value);
  Py_DECREF(Value);
  return Result == 0;
}

PyObject *pointerObject(const void *Pointer) {
  return PyLong_FromVoidPtr(const_cast<void *>(Pointer));
}

PyObject *handleObject(NevercHandle Handle) {
  PyObject *Tuple = PyTuple_New(2);
  if (!Tuple)
    return nullptr;
  PyObject *Owner = PyLong_FromUnsignedLongLong(Handle.Owner);
  PyObject *Value = PyLong_FromUnsignedLongLong(Handle.Value);
  if (!Owner || !Value) {
    Py_XDECREF(Owner);
    Py_XDECREF(Value);
    Py_DECREF(Tuple);
    return nullptr;
  }
  PyTuple_SET_ITEM(Tuple, 0, Owner);
  PyTuple_SET_ITEM(Tuple, 1, Value);
  return Tuple;
}

class PythonCallbackBinding {
public:
  static constexpr uint64_t MagicValue = UINT64_C(0x4e43505942494e44);

  PythonCallbackBinding(std::shared_ptr<PythonPluginRuntimeToken> TokenValue,
                        const NevercCoreAPI *CoreValue,
                        std::string PluginIDValue, std::string RecordNameValue,
                        PyObject *CallbacksValue,
                        std::vector<uint8_t> BytesValue)
      : Identity(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this))),
        Token(std::move(TokenValue)), Core(CoreValue),
        PluginID(std::move(PluginIDValue)),
        RecordName(std::move(RecordNameValue)), Callbacks(CallbacksValue),
        Bytes(std::move(BytesValue)) {
    Py_INCREF(Callbacks);
  }

  ~PythonCallbackBinding() {
    GILGuard Guard;
    invokeOriginalDestroy();
    Py_CLEAR(Callbacks);
    Magic = 0;
  }

  bool valid() const { return Magic == MagicValue; }
  uint64_t identity() const { return Identity; }
  uint64_t runtimeIdentity() const {
    auto Owner = Token.lock();
    return Owner ? Owner->Identity : 0;
  }
  std::shared_ptr<PythonPluginRuntimeToken> token() const {
    return Token.lock();
  }
  const NevercCoreAPI *core() const { return Core; }
  const std::string &pluginID() const { return PluginID; }
  const std::string &recordName() const { return RecordName; }
  std::vector<uint8_t> &bytes() { return Bytes; }
  const std::vector<uint8_t> &bytes() const { return Bytes; }

  bool hasCallback(const char *Name) const {
    return Callbacks && PyDict_GetItemString(Callbacks, Name) != nullptr;
  }

  bool validateCallbacks(std::initializer_list<const char *> Allowed,
                         std::string &Error) const {
    if (!Callbacks || !PyDict_Check(Callbacks)) {
      Error = "callbacks must be a dict";
      return false;
    }
    Py_ssize_t Position = 0;
    PyObject *Key = nullptr;
    PyObject *Value = nullptr;
    while (PyDict_Next(Callbacks, &Position, &Key, &Value)) {
      if (!PyUnicode_Check(Key)) {
        Error = "callback names must be strings";
        return false;
      }
      const char *Name = PyUnicode_AsUTF8(Key);
      if (!Name)
        return false;
      if (std::find_if(Allowed.begin(), Allowed.end(), [&](const char *Item) {
            return std::strcmp(Item, Name) == 0;
          }) == Allowed.end()) {
        Error = "callback is not a field of " + RecordName + ": " + Name;
        return false;
      }
      if (!PyCallable_Check(Value)) {
        Error = "callback is not callable: " + std::string(Name);
        return false;
      }
    }
    return true;
  }

  PyObject *callbackForSymbol(const char *Symbol) const {
    const char *Field = std::strrchr(Symbol, '.');
    Field = Field ? Field + 1 : Symbol;
    PyObject *Callback =
        Callbacks ? PyDict_GetItemString(Callbacks, Field) : nullptr;
    Py_XINCREF(Callback);
    return Callback;
  }

  void setOriginalUserData(void *UserData, NevercDestroyUserDataFn Destroy) {
    OriginalUserData = UserData;
    OriginalDestroy = Destroy;
  }

  void markTransferred() { Transferred.store(true, std::memory_order_release); }
  bool transferred() const {
    return Transferred.load(std::memory_order_acquire);
  }

  void markHostDestroyed() {
    bool Expected = false;
    if (HostDestroyed.compare_exchange_strong(Expected, true,
                                              std::memory_order_acq_rel))
      invokeOriginalDestroy();
  }

private:
  void invokeOriginalDestroy() {
    bool Expected = false;
    if (!OriginalDestroy || !OriginalDestroyInvoked.compare_exchange_strong(
                                Expected, true, std::memory_order_acq_rel))
      return;
    OriginalDestroy(OriginalUserData);
  }

  uint64_t Magic = MagicValue;
  uint64_t Identity = 0;
  std::weak_ptr<PythonPluginRuntimeToken> Token;
  const NevercCoreAPI *Core = nullptr;
  std::string PluginID;
  std::string RecordName;
  PyObject *Callbacks = nullptr;
  std::vector<uint8_t> Bytes;
  void *OriginalUserData = nullptr;
  NevercDestroyUserDataFn OriginalDestroy = nullptr;
  std::atomic<bool> OriginalDestroyInvoked{false};
  std::atomic<bool> HostDestroyed{false};
  std::atomic<bool> Transferred{false};
};

struct PythonPluginFFIState {
  std::mutex BindingsMutex;
  std::unordered_map<uint64_t, std::shared_ptr<PythonCallbackBinding>> Bindings;
};

PythonPluginFFIState *
ffiState(const std::shared_ptr<PythonPluginRuntimeToken> &Token) {
  return Token ? static_cast<PythonPluginFFIState *>(Token->FFIState.get())
               : nullptr;
}

std::shared_ptr<PythonCallbackBinding>
findBinding(const std::shared_ptr<PythonPluginRuntimeToken> &Token,
            uint64_t Identity) {
  PythonPluginFFIState *State = ffiState(Token);
  if (!State)
    return nullptr;
  std::lock_guard<std::mutex> Lock(State->BindingsMutex);
  auto Iterator = State->Bindings.find(Identity);
  return Iterator == State->Bindings.end() ? nullptr : Iterator->second;
}

NevercStatus callbackFailure(PythonCallbackBinding &Binding,
                             const char *Symbol) {
  std::string Traceback = formatPythonException();
  std::string Message =
      "Python callback '" + std::string(Symbol) + "' raised:\n" + Traceback;
  NevercDiagnosticHandle Diagnostic{};
  const NevercCoreAPI *Core = Binding.core();
  if (Core && Core->EmitDiagnostic) {
    NevercDiagnosticDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_CORE_API_MAJOR,
                         NEVERC_CORE_API_MINOR, 0};
    Descriptor.Severity = NEVERC_DIAGNOSTIC_ERROR;
    Descriptor.PluginID = stringView(Binding.pluginID());
    std::string Phase(Symbol);
    Descriptor.PhaseID = stringView(Phase);
    Descriptor.Message = stringView(Message);
    NevercStatus Emitted =
        Core->EmitDiagnostic(Core->Context, &Descriptor, &Diagnostic);
    if (Emitted.Code != NEVERC_STATUS_OK)
      Diagnostic = {};
  }
  return makeStatus(NEVERC_STATUS_PLUGIN_EXCEPTION, 0, Diagnostic.Value);
}

PyObject *callbackArgumentObject(const PythonCallbackArgument &Argument) {
  switch (Argument.Kind) {
  case PythonCallbackArgumentKind::Signed:
    return PyLong_FromLongLong(Argument.SignedValue);
  case PythonCallbackArgumentKind::Unsigned:
    return PyLong_FromUnsignedLongLong(Argument.UnsignedValue);
  case PythonCallbackArgumentKind::Pointer:
    return pointerObject(Argument.PointerValue);
  case PythonCallbackArgumentKind::Record:
    if (!Argument.RecordData && Argument.RecordSize != 0) {
      PyErr_SetString(PyExc_RuntimeError,
                      "NeverC callback carried a null by-value record");
      return nullptr;
    }
    return PyBytes_FromStringAndSize(
        static_cast<const char *>(Argument.RecordData),
        static_cast<Py_ssize_t>(Argument.RecordSize));
  }
  PyErr_SetString(PyExc_RuntimeError, "unknown NeverC callback argument kind");
  return nullptr;
}

void populateCallbackContext(PythonPluginNativeContext &Context,
                             const PythonCallbackArgument *Arguments,
                             size_t ArgumentCount) {
  for (size_t Index = 0; Index != ArgumentCount; ++Index) {
    const PythonCallbackArgument &Argument = Arguments[Index];
    const char *Type = Argument.TypeName ? Argument.TypeName : "";
    if (Argument.Kind == PythonCallbackArgumentKind::Record &&
        std::strcmp(Type, "NevercTaskHandle") == 0 &&
        Argument.RecordSize == sizeof(NevercTaskHandle)) {
      std::memcpy(&Context.Task, Argument.RecordData, sizeof(Context.Task));
      Context.CapabilityMask |= PythonCapabilityTask;
      continue;
    }
    if (Argument.Kind != PythonCallbackArgumentKind::Pointer ||
        !Argument.PointerValue)
      continue;
    if (std::strcmp(Type, "const NevercPhaseFrame *") == 0 ||
        std::strcmp(Type, "NevercPhaseFrame *") == 0) {
      Context.Frame =
          static_cast<const NevercPhaseFrame *>(Argument.PointerValue);
      Context.Session = Context.Frame->Session;
      Context.Task = Context.Frame->Task;
      Context.CapabilityMask |= PythonCapabilityFrame |
                                PythonCapabilitySession | PythonCapabilityTask;
    } else if (std::strcmp(Type, "NevercPhaseContinuation *") == 0 ||
               std::strcmp(Type, "const NevercPhaseContinuation *") == 0) {
      Context.Continuation =
          static_cast<const NevercPhaseContinuation *>(Argument.PointerValue);
      Context.CapabilityMask |= PythonCapabilityContinuation;
    } else if (std::strcmp(Type, "const NevercIRPassInvocation *") == 0 ||
               std::strcmp(Type, "NevercIRPassInvocation *") == 0) {
      auto *Invocation =
          static_cast<const NevercIRPassInvocation *>(Argument.PointerValue);
      Context.Invocation = Invocation;
      Context.Task = Invocation->Task;
      Context.CapabilityMask |=
          PythonCapabilityInvocation | PythonCapabilityTask;
    } else if (std::strcmp(Type, "const NevercMIRPassInvocation *") == 0 ||
               std::strcmp(Type, "NevercMIRPassInvocation *") == 0) {
      auto *Invocation =
          static_cast<const NevercMIRPassInvocation *>(Argument.PointerValue);
      Context.Invocation = Invocation;
      Context.Task = Invocation->Task;
      Context.CapabilityMask |=
          PythonCapabilityInvocation | PythonCapabilityTask;
    }
  }
}

PyObject *invokePythonCallback(PythonCallbackBinding &Binding,
                               const char *Symbol,
                               const PythonCallbackArgument *Arguments,
                               size_t ArgumentCount,
                               PythonPluginNativeContext **OutNative) {
  PyRef Callback(Binding.callbackForSymbol(Symbol));
  if (!Callback) {
    if (std::strstr(Symbol, ".DestroyUserData")) {
      Py_INCREF(Py_None);
      return Py_None;
    }
    PyErr_Format(PyExc_RuntimeError,
                 "NeverC callback binding has no callable for %s", Symbol);
    return nullptr;
  }

  auto Context = std::make_unique<PythonPluginNativeContext>();
  Context->Kind = PythonContextKind::Callback;
  Context->Token = Binding.token();
  Context->Core = Binding.core();
  Context->CapabilityMask = Binding.core() ? PythonCapabilityCore : 0;
  Context->PluginID = Binding.pluginID();
  Context->PhaseName = Symbol;
  populateCallbackContext(*Context, Arguments, ArgumentCount);

  PythonPluginNativeContext *Native = nullptr;
  PyRef Capsule(makePythonContextCapsule(std::move(Context), &Native));
  PyRef Module(PyImport_ImportModule("neverc_plugin.ffi"));
  PyRef ScopeClass(Module ? PyObject_GetAttrString(Module.get(), "Scope")
                          : nullptr);
  PyRef Scope(ScopeClass && Capsule
                  ? PyObject_CallFunctionObjArgs(ScopeClass.get(),
                                                 Capsule.get(), nullptr)
                  : nullptr);
  if (!Scope) {
    if (Native)
      Native->Active = false;
    return nullptr;
  }

  PyRef Positional(PyTuple_New(static_cast<Py_ssize_t>(ArgumentCount + 1)));
  if (!Positional) {
    Native->Active = false;
    return nullptr;
  }
  PyTuple_SET_ITEM(Positional.get(), 0, Scope.release());
  for (size_t Index = 0; Index != ArgumentCount; ++Index) {
    PyObject *Value = callbackArgumentObject(Arguments[Index]);
    if (!Value) {
      Native->Active = false;
      return nullptr;
    }
    PyTuple_SET_ITEM(Positional.get(), static_cast<Py_ssize_t>(Index + 1),
                     Value);
  }
  PyObject *Result = PyObject_CallObject(Callback.get(), Positional.get());
  Native->Active = false;
  if (OutNative)
    *OutNative = Native;
  return Result;
}

bool statusFromPython(PyObject *Object, NevercStatus &Out) {
  if (Object == Py_None) {
    Out = neverc_status_ok();
    return true;
  }
  if (PyBool_Check(Object)) {
    Out = Object == Py_True ? neverc_status_ok()
                            : makeStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
    return true;
  }

  PyObject *Code = nullptr;
  PyObject *Flags = nullptr;
  PyObject *Detail = nullptr;
  PyRef OwnedCode;
  PyRef OwnedFlags;
  PyRef OwnedDetail;
  if (PyLong_Check(Object)) {
    Code = Object;
  } else if (PyTuple_Check(Object) && PyTuple_GET_SIZE(Object) == 3) {
    Code = PyTuple_GET_ITEM(Object, 0);
    Flags = PyTuple_GET_ITEM(Object, 1);
    Detail = PyTuple_GET_ITEM(Object, 2);
  } else {
    OwnedCode = PyRef(PyObject_GetAttrString(Object, "Code"));
    OwnedFlags = PyRef(PyObject_GetAttrString(Object, "Flags"));
    OwnedDetail = PyRef(PyObject_GetAttrString(Object, "Detail"));
    if (!OwnedCode || !OwnedFlags || !OwnedDetail)
      return false;
    Code = OwnedCode.get();
    Flags = OwnedFlags.get();
    Detail = OwnedDetail.get();
  }
  if (!PyLong_Check(Code) || (Flags && !PyLong_Check(Flags)) ||
      (Detail && !PyLong_Check(Detail))) {
    PyErr_SetString(PyExc_TypeError,
                    "NeverC status result fields must be integers");
    return false;
  }
  long long CodeValue = PyLong_AsLongLong(Code);
  if (PyErr_Occurred() || CodeValue < std::numeric_limits<int32_t>::min() ||
      CodeValue > std::numeric_limits<int32_t>::max()) {
    if (!PyErr_Occurred())
      PyErr_SetString(PyExc_OverflowError,
                      "NeverC status code does not fit int32");
    return false;
  }
  unsigned long FlagsValue = Flags ? PyLong_AsUnsignedLong(Flags) : 0;
  unsigned long long DetailValue =
      Detail ? PyLong_AsUnsignedLongLong(Detail) : 0;
  if (PyErr_Occurred() || FlagsValue > UINT32_MAX)
    return false;
  Out = makeStatus(static_cast<NevercStatusCode>(CodeValue),
                   static_cast<uint32_t>(FlagsValue),
                   static_cast<uint64_t>(DetailValue));
  return true;
}

} // namespace

std::shared_ptr<PythonPluginRuntimeToken>
makePythonRuntimeToken(void *Runtime) {
  static_assert(sizeof(uintptr_t) <= sizeof(uint64_t));
  auto Token = std::make_shared<PythonPluginRuntimeToken>();
  Token->Runtime.store(Runtime, std::memory_order_release);
  Token->Identity =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Token.get()));
  Token->FFIState = std::make_shared<PythonPluginFFIState>();
  return Token;
}

namespace {

void contextCapsuleDestructor(PyObject *Capsule) {
  void *Pointer = PyCapsule_GetPointer(Capsule, PythonContextCapsuleName);
  if (!Pointer) {
    PyErr_Clear();
    return;
  }
  delete static_cast<PythonPluginNativeContext *>(Pointer);
}

} // namespace

PyObject *
makePythonContextCapsule(std::unique_ptr<PythonPluginNativeContext> Context,
                         PythonPluginNativeContext **OutContext) {
  PythonPluginNativeContext *Raw = Context.get();
  Raw->Identity = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Raw));
  PyObject *Capsule =
      PyCapsule_New(Raw, PythonContextCapsuleName, contextCapsuleDestructor);
  if (!Capsule)
    return nullptr;
  (void)Context.release();
  if (OutContext)
    *OutContext = Raw;
  return Capsule;
}

PythonPluginNativeContext *checkedPythonContext(PyObject *Object) {
  auto *Context = static_cast<PythonPluginNativeContext *>(
      PyCapsule_GetPointer(Object, PythonContextCapsuleName));
  if (!Context)
    return nullptr;
  if (!Context->Active || !Context->Token ||
      !Context->Token->Runtime.load(std::memory_order_acquire)) {
    PyErr_SetString(PyExc_RuntimeError, "NeverC context is no longer active");
    return nullptr;
  }
  return Context;
}

NevercStatus invokePythonStatusCallback(void *UserData, const char *Symbol,
                                        const PythonCallbackArgument *Arguments,
                                        size_t ArgumentCount) {
  auto *Binding = static_cast<PythonCallbackBinding *>(UserData);
  auto Token = Binding && Binding->valid() ? Binding->token() : nullptr;
  if (!Token || !Token->Runtime.load(std::memory_order_acquire))
    return makeStatus(NEVERC_STATUS_STALE_HANDLE);
  GILGuard Guard;
  PyRef Result(invokePythonCallback(*Binding, Symbol, Arguments, ArgumentCount,
                                    nullptr));
  if (!Result)
    return callbackFailure(*Binding, Symbol);
  NevercStatus Converted{};
  if (!statusFromPython(Result.get(), Converted))
    return callbackFailure(*Binding, Symbol);
  return Converted;
}

void invokePythonVoidCallback(void *UserData, const char *Symbol,
                              const PythonCallbackArgument *Arguments,
                              size_t ArgumentCount) {
  auto *Binding = static_cast<PythonCallbackBinding *>(UserData);
  auto Token = Binding && Binding->valid() ? Binding->token() : nullptr;
  if (!Token || !Token->Runtime.load(std::memory_order_acquire))
    return;
  GILGuard Guard;
  PyRef Result(invokePythonCallback(*Binding, Symbol, Arguments, ArgumentCount,
                                    nullptr));
  if (!Result) {
    (void)callbackFailure(*Binding, Symbol);
    return;
  }
  if (Result.get() != Py_None) {
    PyErr_Format(PyExc_TypeError, "Python void callback '%s' must return None",
                 Symbol);
    (void)callbackFailure(*Binding, Symbol);
  }
}

void pythonCallbackUserDataDestroyed(void *UserData) {
  auto *Binding = static_cast<PythonCallbackBinding *>(UserData);
  if (Binding && Binding->valid())
    Binding->markHostDestroyed();
}

// The generated file defines all 75 exact signatures and the descriptor
// configurator. It intentionally lives in this translation unit so bindings,
// GIL ownership, and exception conversion have one implementation boundary.
#include "PythonPluginTrampolines.inc"

void destroyPythonRuntimeBindings(
    const std::shared_ptr<PythonPluginRuntimeToken> &Token) {
  if (Token)
    Token->FFIState.reset();
}

PyObject *pythonContextCapabilities(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  if (!PyArg_ParseTuple(Arguments, "O:context_capabilities", &Handle))
    return nullptr;
  PythonPluginNativeContext *Context = checkedPythonContext(Handle);
  if (!Context)
    return nullptr;

  PyObject *Result = PyDict_New();
  if (!Result)
    return nullptr;
  if (!setOwned(Result, "runtime_id",
                PyLong_FromUnsignedLongLong(Context->Token->Identity)) ||
      !setOwned(Result, "context_id",
                PyLong_FromUnsignedLongLong(Context->Identity)) ||
      !setOwned(
          Result, "kind",
          PyLong_FromUnsignedLong(static_cast<uint32_t>(Context->Kind))) ||
      !setOwned(Result, "mask",
                PyLong_FromUnsignedLongLong(Context->CapabilityMask)) ||
      !setOwned(Result, "core_address", pointerObject(Context->Core)) ||
      !setOwned(
          Result, "core_context_address",
          pointerObject(Context->Core ? Context->Core->Context : nullptr)) ||
      !setOwned(Result, "registrar_address",
                pointerObject(Context->Registrar)) ||
      !setOwned(Result, "registrar_context_address",
                pointerObject(Context->RegistrarContext)) ||
      !setOwned(Result, "session", handleObject(Context->Session)) ||
      !setOwned(Result, "task", handleObject(Context->Task)) ||
      !setOwned(Result, "frame_address", pointerObject(Context->Frame)) ||
      !setOwned(Result, "continuation_address",
                pointerObject(Context->Continuation)) ||
      !setOwned(Result, "invocation_address",
                pointerObject(Context->Invocation))) {
    Py_DECREF(Result);
    return nullptr;
  }
  return Result;
}

PyObject *pythonContextIsActive(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  unsigned long long RuntimeIdentity = 0;
  unsigned long long ContextIdentity = 0;
  if (!PyArg_ParseTuple(Arguments, "OKK:context_is_active", &Handle,
                        &RuntimeIdentity, &ContextIdentity))
    return nullptr;
  auto *Context = static_cast<PythonPluginNativeContext *>(
      PyCapsule_GetPointer(Handle, PythonContextCapsuleName));
  if (!Context)
    return nullptr;
  bool Active = Context->Active && Context->Token &&
                Context->Token->Runtime.load(std::memory_order_acquire) &&
                Context->Token->Identity == RuntimeIdentity &&
                Context->Identity == ContextIdentity;
  if (Active)
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

PyObject *pythonBindCallbackRecord(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  const char *RecordData = nullptr;
  Py_ssize_t RecordLength = 0;
  const char *ByteData = nullptr;
  Py_ssize_t ByteLength = 0;
  PyObject *Callbacks = nullptr;
  if (!PyArg_ParseTuple(Arguments, "Os#y#O:bind_callback_record", &Handle,
                        &RecordData, &RecordLength, &ByteData, &ByteLength,
                        &Callbacks))
    return nullptr;
  PythonPluginNativeContext *Context = checkedPythonContext(Handle);
  if (!Context)
    return nullptr;
  if (!PyDict_Check(Callbacks)) {
    PyErr_SetString(PyExc_TypeError, "callbacks must be a dict");
    return nullptr;
  }
  std::string RecordName(RecordData, static_cast<size_t>(RecordLength));
  std::vector<uint8_t> Bytes(reinterpret_cast<const uint8_t *>(ByteData),
                             reinterpret_cast<const uint8_t *>(ByteData) +
                                 ByteLength);
  auto Binding = std::make_shared<PythonCallbackBinding>(
      Context->Token, Context->Core, Context->PluginID, RecordName, Callbacks,
      std::move(Bytes));
  uint64_t Identity = Binding->identity();
  std::string Error;
  if (!configurePythonCallbackRecord(*Binding, RecordName, Binding->bytes(),
                                     Error)) {
    if (!PyErr_Occurred())
      PyErr_SetString(PyExc_ValueError, Error.c_str());
    return nullptr;
  }
  PythonPluginFFIState *State = ffiState(Context->Token);
  if (!State) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC Python FFI state is no longer active");
    return nullptr;
  }
  {
    std::lock_guard<std::mutex> Lock(State->BindingsMutex);
    State->Bindings.emplace(Identity, Binding);
  }
  PyObject *Result = PyTuple_New(2);
  if (!Result) {
    std::lock_guard<std::mutex> Lock(State->BindingsMutex);
    State->Bindings.erase(Identity);
    return nullptr;
  }
  PyObject *IdentityObject = PyLong_FromUnsignedLongLong(Identity);
  PyObject *AddressObject = pointerObject(Binding->bytes().data());
  if (!IdentityObject || !AddressObject) {
    Py_XDECREF(IdentityObject);
    Py_XDECREF(AddressObject);
    Py_DECREF(Result);
    std::lock_guard<std::mutex> Lock(State->BindingsMutex);
    State->Bindings.erase(Identity);
    return nullptr;
  }
  PyTuple_SET_ITEM(Result, 0, IdentityObject);
  PyTuple_SET_ITEM(Result, 1, AddressObject);
  return Result;
}

PyObject *pythonTransferCallbackBinding(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  unsigned long long Identity = 0;
  if (!PyArg_ParseTuple(Arguments, "OK:transfer_callback_binding", &Handle,
                        &Identity))
    return nullptr;
  PythonPluginNativeContext *Context = checkedPythonContext(Handle);
  if (!Context)
    return nullptr;
  auto Binding = findBinding(Context->Token, Identity);
  if (!Binding || Binding->runtimeIdentity() != Context->Token->Identity) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC callback binding does not belong to this runtime");
    return nullptr;
  }
  Binding->markTransferred();
  Py_RETURN_NONE;
}

PyObject *pythonReleaseCallbackBinding(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  unsigned long long Identity = 0;
  if (!PyArg_ParseTuple(Arguments, "OK:release_callback_binding", &Handle,
                        &Identity))
    return nullptr;
  PythonPluginNativeContext *Context = checkedPythonContext(Handle);
  if (!Context)
    return nullptr;
  std::shared_ptr<PythonCallbackBinding> Removed;
  PythonPluginFFIState *State = ffiState(Context->Token);
  if (!State) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC Python FFI state is no longer active");
    return nullptr;
  }
  {
    std::lock_guard<std::mutex> Lock(State->BindingsMutex);
    auto Iterator = State->Bindings.find(Identity);
    if (Iterator == State->Bindings.end() ||
        Iterator->second->runtimeIdentity() != Context->Token->Identity) {
      PyErr_SetString(
          PyExc_RuntimeError,
          "NeverC callback binding does not belong to this runtime");
      return nullptr;
    }
    if (Iterator->second->transferred()) {
      PyErr_SetString(PyExc_RuntimeError,
                      "NeverC callback binding ownership was transferred");
      return nullptr;
    }
    Removed = std::move(Iterator->second);
    State->Bindings.erase(Iterator);
  }
  Py_RETURN_NONE;
}

} // namespace neverc::plugin
