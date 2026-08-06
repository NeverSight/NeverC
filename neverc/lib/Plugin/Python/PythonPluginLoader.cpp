#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "PythonPluginLoader.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr const char *BridgeModuleName = "_neverc_plugin";
constexpr const char *ContextCapsuleName = "neverc_plugin.context.v1";

Error pythonPluginError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus status(NevercStatusCode Code, uint64_t Detail = 0) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  Result.Detail = Detail;
  return Result;
}

class PyRef {
public:
  PyRef() = default;
  explicit PyRef(PyObject *Value) : Object(Value) {}
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

class GILGuard {
public:
  GILGuard() : State(PyGILState_Ensure()) {}
  ~GILGuard() { PyGILState_Release(State); }

  GILGuard(const GILGuard &) = delete;
  GILGuard &operator=(const GILGuard &) = delete;

private:
  PyGILState_STATE State;
};

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

/// Consume and format the active Python exception, including its traceback.
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
  if (!Module) {
    PyErr_Clear();
    return fallbackPythonException(Value.get());
  }
  PyRef Formatter(PyObject_GetAttrString(Module.get(), "format_exception"));
  if (!Formatter) {
    PyErr_Clear();
    return fallbackPythonException(Value.get());
  }
  PyObject *TracebackArgument = Traceback ? Traceback.get() : Py_None;
  PyRef Lines(PyObject_CallFunctionObjArgs(
      Formatter.get(), Type ? Type.get() : Py_None,
      Value ? Value.get() : Py_None, TracebackArgument, nullptr));
  if (!Lines) {
    PyErr_Clear();
    return fallbackPythonException(Value.get());
  }
  PyRef Separator(PyUnicode_FromString(""));
  PyRef Joined(Separator ? PyUnicode_Join(Separator.get(), Lines.get())
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

PyObject *registerOption(PyObject *, PyObject *Arguments);
PyObject *registerObserver(PyObject *, PyObject *Arguments);
PyObject *optionValues(PyObject *, PyObject *Arguments);
PyObject *checkCancelled(PyObject *, PyObject *Arguments);
PyObject *emitDiagnostic(PyObject *, PyObject *Arguments);
PyObject *frameArguments(PyObject *, PyObject *Arguments);

struct BridgeState {
  uint64_t NextModuleID;
};

PyModuleDef BridgeModule = {
    PyModuleDef_HEAD_INIT,
    BridgeModuleName,
    "Private in-process bridge for NeverC Python plugins.",
    sizeof(BridgeState),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

bool prepareBridgeMethods() {
  if (BridgeModule.m_methods)
    return true;
  PyMethodDef *Methods = PyMem_New(PyMethodDef, 7);
  if (!Methods) {
    PyErr_NoMemory();
    return false;
  }
  Methods[0] = {"register_option", registerOption, METH_VARARGS,
                "Register a validated NeverC plugin option."};
  Methods[1] = {"register_observer", registerObserver, METH_VARARGS,
                "Register a read-only NeverC phase observer."};
  Methods[2] = {"option_values", optionValues, METH_VARARGS,
                "Return parsed values for a plugin option."};
  Methods[3] = {"check_cancelled", checkCancelled, METH_VARARGS,
                "Raise when the current NeverC task is cancelled."};
  Methods[4] = {"emit_diagnostic", emitDiagnostic, METH_VARARGS,
                "Emit a structured NeverC diagnostic."};
  Methods[5] = {"frame_arguments", frameArguments, METH_VARARGS,
                "Read raw driver arguments from a phase frame."};
  Methods[6] = {nullptr, nullptr, 0, nullptr};
  BridgeModule.m_methods = Methods;
  return true;
}

bool installBridgeModule() {
  PyObject *Modules = PyImport_GetModuleDict();
  if (!Modules)
    return false;
  PyObject *Existing = PyDict_GetItemString(Modules, BridgeModuleName);
  if (Existing) {
    int IsBridge = PyObject_HasAttrString(Existing, "_neverc_bridge_v1");
    if (IsBridge == 1)
      return true;
    if (IsBridge < 0)
      return false;
    PyErr_SetString(PyExc_ImportError, "sys.modules already contains a foreign "
                                       "'_neverc_plugin' module");
    return false;
  }

  if (!prepareBridgeMethods())
    return false;
  PyRef Module(PyModule_Create(&BridgeModule));
  auto *State =
      Module ? static_cast<BridgeState *>(PyModule_GetState(Module.get()))
             : nullptr;
  if (!Module || !State ||
      PyModule_AddIntConstant(Module.get(), "_neverc_bridge_v1", 1) < 0 ||
      PyDict_SetItemString(Modules, BridgeModuleName, Module.get()) < 0)
    return false;
  State->NextModuleID = 1;
  return true;
}

std::once_flag InterpreterOnce;

std::string initializeInterpreter() {
  std::string InitializationError;
  std::call_once(InterpreterOnce, [&] {
    if (Py_IsInitialized())
      return;

    PyConfig Config;
    PyConfig_InitPythonConfig(&Config);
    Config.parse_argv = 0;
    Config.install_signal_handlers = 0;
    Config.write_bytecode = 0;
    PyStatus Initialization = Py_InitializeFromConfig(&Config);
    if (PyStatus_Exception(Initialization)) {
      InitializationError = Initialization.err_msg
                                ? Initialization.err_msg
                                : "CPython initialization failed";
      PyConfig_Clear(&Config);
      return;
    }
    PyConfig_Clear(&Config);
    (void)PyEval_SaveThread();
  });
  return InitializationError;
}

bool prependPythonPath(StringRef Path) {
  PyObject *SearchPath = PySys_GetObject("path"); // Borrowed.
  if (!SearchPath || !PyList_Check(SearchPath)) {
    PyErr_SetString(PyExc_RuntimeError, "Python sys.path is unavailable");
    return false;
  }
  PyRef Item(PyUnicode_DecodeFSDefaultAndSize(
      Path.data(), static_cast<Py_ssize_t>(Path.size())));
  if (!Item)
    return false;
  int Contains = PySequence_Contains(SearchPath, Item.get());
  if (Contains < 0)
    return false;
  return Contains == 1 || PyList_Insert(SearchPath, 0, Item.get()) == 0;
}

void addAdjacentSDKPath() {
  std::string Executable =
      sys::fs::getMainExecutable(nullptr, static_cast<void *>(&BridgeModule));
  if (Executable.empty())
    return;
  SmallString<256> SDKPath(Executable);
  sys::path::remove_filename(SDKPath);
  sys::path::append(SDKPath, "..", "pluginsdk", "python");
  if (!sys::fs::is_directory(SDKPath))
    return;
  if (!prependPythonPath(SDKPath))
    PyErr_Clear();
}

class PythonPluginRuntime;

struct RuntimeToken {
  PythonPluginRuntime *Runtime = nullptr;
};

enum class ContextKind {
  Registration,
  Process,
  Session,
  Task,
  Frame,
};

struct NativeContext {
  ContextKind Kind = ContextKind::Process;
  bool Active = true;
  std::shared_ptr<RuntimeToken> Token;
  const NevercCoreAPI *Core = nullptr;
  const NevercRegistrarAPI *Registrar = nullptr;
  void *RegistrarContext = nullptr;
  NevercSessionHandle Session{};
  NevercTaskHandle Task{};
  const NevercPhaseFrame *Frame = nullptr;
  std::string PhaseName;
};

void contextCapsuleDestructor(PyObject *Capsule) {
  void *Pointer = PyCapsule_GetPointer(Capsule, ContextCapsuleName);
  if (!Pointer) {
    PyErr_Clear();
    return;
  }
  delete static_cast<NativeContext *>(Pointer);
}

PyObject *makeContextCapsule(std::unique_ptr<NativeContext> Context,
                             NativeContext **OutContext) {
  NativeContext *Raw = Context.get();
  PyObject *Capsule =
      PyCapsule_New(Raw, ContextCapsuleName, contextCapsuleDestructor);
  if (!Capsule)
    return nullptr;
  (void)Context.release();
  if (OutContext)
    *OutContext = Raw;
  return Capsule;
}

NativeContext *checkedContext(PyObject *Object) {
  auto *Context = static_cast<NativeContext *>(
      PyCapsule_GetPointer(Object, ContextCapsuleName));
  if (!Context)
    return nullptr;
  if (!Context->Active || !Context->Token || !Context->Token->Runtime) {
    PyErr_SetString(PyExc_RuntimeError, "NeverC context is no longer active");
    return nullptr;
  }
  return Context;
}

bool isKind(const NativeContext &Context,
            std::initializer_list<ContextKind> Kinds) {
  for (ContextKind Kind : Kinds)
    if (Context.Kind == Kind)
      return true;
  return false;
}

bool dictionarySet(PyObject *Dictionary, const char *Name, PyObject *Value) {
  if (!Value)
    return false;
  int Result = PyDict_SetItemString(Dictionary, Name, Value);
  Py_DECREF(Value);
  return Result == 0;
}

PyObject *handleTuple(NevercHandle Handle) {
  PyRef Owner(PyLong_FromUnsignedLongLong(Handle.Owner));
  PyRef Value(PyLong_FromUnsignedLongLong(Handle.Value));
  if (!Owner || !Value)
    return nullptr;
  PyObject *Tuple = PyTuple_New(2);
  if (!Tuple)
    return nullptr;
  PyTuple_SET_ITEM(Tuple, 0, Owner.release());
  PyTuple_SET_ITEM(Tuple, 1, Value.release());
  return Tuple;
}

PyObject *stringViewObject(NevercStringView View) {
  if ((!View.Data && View.Length != 0) ||
      View.Length > static_cast<uint64_t>(PY_SSIZE_T_MAX)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC returned an invalid string view");
    return nullptr;
  }
  return PyUnicode_DecodeUTF8(View.Data ? View.Data : "",
                              static_cast<Py_ssize_t>(View.Length), "strict");
}

PyObject *frameInfo(const NevercPhaseFrame &Frame, StringRef PhaseName,
                    NevercObserverPoint Point) {
  PyRef Dictionary(PyDict_New());
  if (!Dictionary)
    return nullptr;
  const char *When = Point == NEVERC_OBSERVER_BEFORE  ? "before"
                     : Point == NEVERC_OBSERVER_AFTER ? "after"
                                                      : "after_commit";
  if (!dictionarySet(Dictionary.get(), "phase_high",
                     PyLong_FromUnsignedLongLong(Frame.Phase.High)) ||
      !dictionarySet(Dictionary.get(), "phase_low",
                     PyLong_FromUnsignedLongLong(Frame.Phase.Low)) ||
      !dictionarySet(
          Dictionary.get(), "phase_name",
          PyUnicode_DecodeUTF8(PhaseName.data(),
                               static_cast<Py_ssize_t>(PhaseName.size()),
                               "strict")) ||
      !dictionarySet(Dictionary.get(), "when", PyUnicode_FromString(When)) ||
      !dictionarySet(Dictionary.get(), "session", handleTuple(Frame.Session)) ||
      !dictionarySet(Dictionary.get(), "task", handleTuple(Frame.Task)) ||
      !dictionarySet(Dictionary.get(), "target_triple",
                     stringViewObject(Frame.Route.TargetTriple)) ||
      !dictionarySet(Dictionary.get(), "cpu",
                     stringViewObject(Frame.Route.CPU)) ||
      !dictionarySet(Dictionary.get(), "features",
                     stringViewObject(Frame.Route.Features)) ||
      !dictionarySet(Dictionary.get(), "object_format",
                     stringViewObject(Frame.Route.ObjectFormat)) ||
      !dictionarySet(Dictionary.get(), "execution_level",
                     PyLong_FromUnsignedLong(Frame.Route.ExecutionLevel)) ||
      !dictionarySet(Dictionary.get(), "input_handle",
                     handleTuple(Frame.Input)) ||
      !dictionarySet(Dictionary.get(), "output_handle",
                     handleTuple(Frame.CurrentOutput)))
    return nullptr;
  return Dictionary.release();
}

PyObject *lifecycleInfo(NevercHandle Handle, NevercTaskKind Kind = 0) {
  PyRef Dictionary(PyDict_New());
  if (!Dictionary ||
      !dictionarySet(Dictionary.get(), "handle", handleTuple(Handle)))
    return nullptr;
  if (Kind != 0 &&
      !dictionarySet(Dictionary.get(), "kind", PyLong_FromUnsignedLong(Kind)))
    return nullptr;
  return Dictionary.release();
}

struct LifecycleState {
  PyObject *Context = nullptr;
  NativeContext *Native = nullptr;
};

struct ObserverBinding {
  std::shared_ptr<RuntimeToken> Token;
  PyObject *Callback = nullptr;
  std::string PhaseName;

  ~ObserverBinding() { Py_XDECREF(Callback); }
};

class PythonPluginRuntime final : public PluginRuntime {
public:
  PythonPluginRuntime(std::string ModuleNameValue, PyObject *ModuleValue,
                      PyObject *ClassValue, PyObject *APIModuleValue,
                      std::string PluginIDValue, bool HasSessionHooksValue,
                      bool HasTaskHooksValue)
      : ModuleName(std::move(ModuleNameValue)), Module(ModuleValue),
        PluginClass(ClassValue), APIModule(APIModuleValue),
        PluginID(std::move(PluginIDValue)),
        HasSessionHooks(HasSessionHooksValue), HasTaskHooks(HasTaskHooksValue),
        Token(std::make_shared<RuntimeToken>()) {
    Token->Runtime = this;
  }

  ~PythonPluginRuntime() override {
    GILGuard Guard;
    Token->Runtime = nullptr;
    if (ProcessNative)
      ProcessNative->Active = false;
    Py_XDECREF(ProcessContext);
    Py_XDECREF(Instance);
    Py_XDECREF(PluginClass);
    Py_XDECREF(APIModule);
    if (!ModuleName.empty()) {
      PyObject *Modules = PyImport_GetModuleDict();
      if (Modules && PyDict_DelItemString(Modules, ModuleName.c_str()) < 0)
        PyErr_Clear();
    }
    Py_XDECREF(Module);
  }

  bool hasProcessBegin() const override { return true; }
  bool hasRegister() const override { return true; }
  // TaskBegin has no session-handle parameter. Keep an internal session
  // context whenever task hooks exist so their checked handles can retain the
  // matching session identity even when the author omitted session hooks.
  bool hasSessionBegin() const override {
    return HasSessionHooks || HasTaskHooks;
  }
  bool hasSessionEnd() const override {
    return HasSessionHooks || HasTaskHooks;
  }
  bool hasTaskBegin() const override { return HasTaskHooks; }
  bool hasTaskEnd() const override { return HasTaskHooks; }
  bool hasDestroy() const override { return true; }

  NevercStatus processBegin(const NevercCoreAPI *Core,
                            void **OutProcessState) override;
  NevercStatus registerPlugin(const NevercCoreAPI *Core,
                              const NevercRegistrarAPI *Registrar,
                              void *RegistrarContext,
                              void *ProcessState) override;
  NevercStatus sessionBegin(const NevercCoreAPI *Core,
                            NevercSessionHandle Session, void *ProcessState,
                            void **OutSessionState) override;
  NevercStatus sessionEnd(const NevercCoreAPI *Core,
                          NevercSessionHandle Session, void *ProcessState,
                          void *SessionState) override;
  NevercStatus taskBegin(const NevercCoreAPI *Core, NevercTaskHandle Task,
                         NevercTaskKind Kind, void *ProcessState,
                         void *SessionState, void **OutTaskState) override;
  NevercStatus taskEnd(const NevercCoreAPI *Core, NevercTaskHandle Task,
                       NevercTaskKind Kind, void *ProcessState,
                       void *SessionState, void *TaskState) override;
  NevercStatus destroy(const NevercCoreAPI *Core, void *ProcessState) override;

  std::string lastError() const override {
    std::lock_guard<std::mutex> Lock(ErrorMutex);
    return LastError;
  }

  StringRef pluginID() const { return PluginID; }
  std::shared_ptr<RuntimeToken> token() const { return Token; }

  NevercStatus invokeObserver(const NevercPhaseFrame *Frame,
                              NevercObserverPoint Point,
                              ObserverBinding &Binding);

private:
  PyObject *createContext(const char *ClassName, ContextKind Kind,
                          const NevercCoreAPI *Core, PyObject *Info,
                          NevercSessionHandle Session, NevercTaskHandle Task,
                          const NevercPhaseFrame *Frame, StringRef PhaseName,
                          const NevercRegistrarAPI *Registrar,
                          void *RegistrarContext, NativeContext **OutNative);
  NevercStatus invokeHook(const char *Name, PyObject *Context,
                          bool MayReturnState, const NevercCoreAPI *Core,
                          StringRef PhaseName);
  NevercStatus pythonFailure(const NevercCoreAPI *Core, StringRef CallbackName,
                             StringRef PhaseName);
  void releaseState(LifecycleState *State);
  void setLastError(std::string Message) {
    std::lock_guard<std::mutex> Lock(ErrorMutex);
    LastError = std::move(Message);
  }
  void clearLastError() {
    std::lock_guard<std::mutex> Lock(ErrorMutex);
    LastError.clear();
  }

  std::string ModuleName;
  PyObject *Module = nullptr;
  PyObject *PluginClass = nullptr;
  PyObject *APIModule = nullptr;
  PyObject *Instance = nullptr;
  PyObject *ProcessContext = nullptr;
  NativeContext *ProcessNative = nullptr;
  const NevercCoreAPI *ProcessCore = nullptr;
  std::string PluginID;
  bool HasSessionHooks = false;
  bool HasTaskHooks = false;
  std::shared_ptr<RuntimeToken> Token;
  mutable std::mutex ErrorMutex;
  std::string LastError;
};

PyObject *PythonPluginRuntime::createContext(
    const char *ClassName, ContextKind Kind, const NevercCoreAPI *Core,
    PyObject *Info, NevercSessionHandle Session, NevercTaskHandle Task,
    const NevercPhaseFrame *Frame, StringRef PhaseName,
    const NevercRegistrarAPI *Registrar, void *RegistrarContext,
    NativeContext **OutNative) {
  PyRef Class(PyObject_GetAttrString(APIModule, ClassName));
  if (!Class)
    return nullptr;
  auto Context = std::make_unique<NativeContext>();
  Context->Kind = Kind;
  Context->Token = Token;
  Context->Core = Core;
  Context->Registrar = Registrar;
  Context->RegistrarContext = RegistrarContext;
  Context->Session = Session;
  Context->Task = Task;
  Context->Frame = Frame;
  Context->PhaseName = PhaseName.str();
  PyRef Capsule(makeContextCapsule(std::move(Context), OutNative));
  if (!Capsule)
    return nullptr;
  if (Info)
    return PyObject_CallFunctionObjArgs(Class.get(), Capsule.get(), Info,
                                        nullptr);
  return PyObject_CallFunctionObjArgs(Class.get(), Capsule.get(), nullptr);
}

NevercStatus PythonPluginRuntime::pythonFailure(const NevercCoreAPI *Core,
                                                StringRef CallbackName,
                                                StringRef PhaseName) {
  std::string Traceback = formatPythonException();
  std::string Message =
      (Twine("Python callback '") + CallbackName + "' raised:\n" + Traceback)
          .str();
  setLastError(Message);

  NevercDiagnosticHandle Diagnostic{};
  if (Core && Core->EmitDiagnostic) {
    NevercDiagnosticDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_CORE_API_MAJOR,
                         NEVERC_CORE_API_MINOR, 0};
    Descriptor.Severity = NEVERC_DIAGNOSTIC_ERROR;
    Descriptor.PluginID = {PluginID.data(), PluginID.size()};
    Descriptor.PhaseID = {PhaseName.data(), PhaseName.size()};
    Descriptor.Message = {Message.data(), Message.size()};
    NevercStatus Emitted =
        Core->EmitDiagnostic(Core->Context, &Descriptor, &Diagnostic);
    if (Emitted.Code != NEVERC_STATUS_OK)
      Diagnostic = {};
  }
  return status(NEVERC_STATUS_PLUGIN_EXCEPTION, Diagnostic.Value);
}

NevercStatus PythonPluginRuntime::invokeHook(const char *Name,
                                             PyObject *Context,
                                             bool MayReturnState,
                                             const NevercCoreAPI *Core,
                                             StringRef PhaseName) {
  PyRef Method(PyObject_GetAttrString(Instance, Name));
  if (!Method) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return neverc_status_ok();
    }
    return pythonFailure(Core, Name, PhaseName);
  }
  if (!PyCallable_Check(Method.get())) {
    PyErr_Format(PyExc_TypeError, "plugin attribute '%s' is not callable",
                 Name);
    return pythonFailure(Core, Name, PhaseName);
  }
  PyRef Result(PyObject_CallFunctionObjArgs(Method.get(), Context, nullptr));
  if (!Result)
    return pythonFailure(Core, Name, PhaseName);
  if (MayReturnState) {
    if (Result.get() != Py_None &&
        PyObject_SetAttrString(Context, "state", Result.get()) < 0)
      return pythonFailure(Core, Name, PhaseName);
    return neverc_status_ok();
  }
  if (Result.get() != Py_None) {
    PyErr_Format(PyExc_TypeError, "plugin hook '%s' must return None", Name);
    return pythonFailure(Core, Name, PhaseName);
  }
  return neverc_status_ok();
}

void PythonPluginRuntime::releaseState(LifecycleState *State) {
  if (!State)
    return;
  if (State->Native)
    State->Native->Active = false;
  Py_XDECREF(State->Context);
  delete State;
}

NevercStatus PythonPluginRuntime::processBegin(const NevercCoreAPI *Core,
                                               void **OutProcessState) {
  if (!OutProcessState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = nullptr;
  GILGuard Guard;
  clearLastError();
  if (Instance) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Python plugin process is already active");
    return pythonFailure(Core, "on_process_begin", {});
  }
  ProcessCore = Core;
  Instance = PyObject_CallNoArgs(PluginClass);
  if (!Instance)
    return pythonFailure(Core, "plugin constructor", {});
  ProcessContext =
      createContext("ProcessContext", ContextKind::Process, Core, nullptr, {},
                    {}, nullptr, {}, nullptr, nullptr, &ProcessNative);
  if (!ProcessContext) {
    ProcessNative = nullptr;
    Py_CLEAR(Instance);
    return pythonFailure(Core, "on_process_begin", {});
  }
  NevercStatus Result =
      invokeHook("on_process_begin", ProcessContext, true, Core, {});
  if (Result.Code != NEVERC_STATUS_OK) {
    ProcessNative->Active = false;
    Py_CLEAR(ProcessContext);
    ProcessNative = nullptr;
    Py_CLEAR(Instance);
    return Result;
  }
  *OutProcessState = this;
  return neverc_status_ok();
}

NevercStatus PythonPluginRuntime::registerPlugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *ProcessState) {
  GILGuard Guard;
  clearLastError();
  if (ProcessState != this || !Instance || !Registrar || !RegistrarContext) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Python plugin registration has invalid process state");
    return pythonFailure(Core, "register", {});
  }
  NativeContext *Native = nullptr;
  PyRef Context(createContext("RegistrationContext", ContextKind::Registration,
                              Core, nullptr, {}, {}, nullptr, {}, Registrar,
                              RegistrarContext, &Native));
  if (!Context)
    return pythonFailure(Core, "register", {});
  NevercStatus Result = invokeHook("register", Context.get(), false, Core, {});
  Native->Active = false;
  return Result;
}

NevercStatus PythonPluginRuntime::sessionBegin(const NevercCoreAPI *Core,
                                               NevercSessionHandle Session,
                                               void *ProcessState,
                                               void **OutSessionState) {
  if (!OutSessionState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSessionState = nullptr;
  GILGuard Guard;
  clearLastError();
  if (ProcessState != this || !Instance)
    return status(NEVERC_STATUS_INVALID_STATE);
  PyRef Info(lifecycleInfo(Session));
  auto State = std::make_unique<LifecycleState>();
  if (!Info)
    return pythonFailure(Core, "on_session_begin", {});
  State->Context =
      createContext("SessionContext", ContextKind::Session, Core, Info.get(),
                    Session, {}, nullptr, {}, nullptr, nullptr, &State->Native);
  if (!State->Context)
    return pythonFailure(Core, "on_session_begin", {});
  NevercStatus Result =
      invokeHook("on_session_begin", State->Context, true, Core, {});
  if (Result.Code != NEVERC_STATUS_OK) {
    releaseState(State.release());
    return Result;
  }
  *OutSessionState = State.release();
  return neverc_status_ok();
}

NevercStatus PythonPluginRuntime::sessionEnd(const NevercCoreAPI *Core,
                                             NevercSessionHandle Session,
                                             void *ProcessState,
                                             void *SessionState) {
  (void)Session;
  GILGuard Guard;
  clearLastError();
  auto *State = static_cast<LifecycleState *>(SessionState);
  if (ProcessState != this || !State || !State->Context)
    return status(NEVERC_STATUS_INVALID_STATE);
  NevercStatus Result =
      invokeHook("on_session_end", State->Context, false, Core, {});
  releaseState(State);
  return Result;
}

NevercStatus
PythonPluginRuntime::taskBegin(const NevercCoreAPI *Core, NevercTaskHandle Task,
                               NevercTaskKind Kind, void *ProcessState,
                               void *SessionState, void **OutTaskState) {
  if (!OutTaskState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutTaskState = nullptr;
  GILGuard Guard;
  clearLastError();
  auto *Session = static_cast<LifecycleState *>(SessionState);
  if (ProcessState != this || !Session || !Session->Native)
    return status(NEVERC_STATUS_INVALID_STATE);
  PyRef Info(lifecycleInfo(Task, Kind));
  auto State = std::make_unique<LifecycleState>();
  if (!Info)
    return pythonFailure(Core, "on_task_begin", {});
  State->Context = createContext("TaskContext", ContextKind::Task, Core,
                                 Info.get(), Session->Native->Session, Task,
                                 nullptr, {}, nullptr, nullptr, &State->Native);
  if (!State->Context)
    return pythonFailure(Core, "on_task_begin", {});
  NevercStatus Result =
      invokeHook("on_task_begin", State->Context, true, Core, {});
  if (Result.Code != NEVERC_STATUS_OK) {
    releaseState(State.release());
    return Result;
  }
  *OutTaskState = State.release();
  return neverc_status_ok();
}

NevercStatus PythonPluginRuntime::taskEnd(const NevercCoreAPI *Core,
                                          NevercTaskHandle Task,
                                          NevercTaskKind Kind,
                                          void *ProcessState,
                                          void *SessionState, void *TaskState) {
  (void)Task;
  (void)Kind;
  (void)SessionState;
  GILGuard Guard;
  clearLastError();
  auto *State = static_cast<LifecycleState *>(TaskState);
  if (ProcessState != this || !State || !State->Context)
    return status(NEVERC_STATUS_INVALID_STATE);
  NevercStatus Result =
      invokeHook("on_task_end", State->Context, false, Core, {});
  releaseState(State);
  return Result;
}

NevercStatus PythonPluginRuntime::destroy(const NevercCoreAPI *Core,
                                          void *ProcessState) {
  GILGuard Guard;
  clearLastError();
  if (ProcessState != this || !Instance)
    return status(NEVERC_STATUS_INVALID_STATE);
  NevercStatus Result =
      invokeHook("on_destroy", ProcessContext, false, Core, {});
  if (ProcessNative)
    ProcessNative->Active = false;
  Py_CLEAR(ProcessContext);
  ProcessNative = nullptr;
  Py_CLEAR(Instance);
  ProcessCore = nullptr;
  return Result;
}

NevercStatus PythonPluginRuntime::invokeObserver(const NevercPhaseFrame *Frame,
                                                 NevercObserverPoint Point,
                                                 ObserverBinding &Binding) {
  GILGuard Guard;
  clearLastError();
  if (!Frame || !Binding.Callback || !ProcessCore) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC invoked an invalid Python observer binding");
    return pythonFailure(ProcessCore, "observer", Binding.PhaseName);
  }
  PyRef Info(frameInfo(*Frame, Binding.PhaseName, Point));
  NativeContext *Native = nullptr;
  PyRef Context;
  if (Info)
    Context = PyRef(createContext(
        "Frame", ContextKind::Frame, ProcessCore, Info.get(), Frame->Session,
        Frame->Task, Frame, Binding.PhaseName, nullptr, nullptr, &Native));
  if (!Context)
    return pythonFailure(ProcessCore, "observer", Binding.PhaseName);
  PyRef Result(
      PyObject_CallFunctionObjArgs(Binding.Callback, Context.get(), nullptr));
  Native->Active = false;
  if (!Result)
    return pythonFailure(ProcessCore, "observer", Binding.PhaseName);
  if (Result.get() != Py_None) {
    PyErr_SetString(PyExc_TypeError, "observer callbacks must return None");
    return pythonFailure(ProcessCore, "observer", Binding.PhaseName);
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL observerCallback(const NevercPhaseFrame *Frame,
                                          NevercObserverPoint Point,
                                          void *UserData) {
  auto *Binding = static_cast<ObserverBinding *>(UserData);
  if (!Binding || !Binding->Token || !Binding->Token->Runtime)
    return status(NEVERC_STATUS_STALE_HANDLE);
  return Binding->Token->Runtime->invokeObserver(Frame, Point, *Binding);
}

void NEVERC_CALL destroyObserverBinding(void *UserData) {
  if (!UserData)
    return;
  GILGuard Guard;
  delete static_cast<ObserverBinding *>(UserData);
}

PyObject *requiredDictionaryItem(PyObject *Dictionary, const char *Name) {
  PyObject *Value = PyDict_GetItemString(Dictionary, Name); // Borrowed.
  if (!Value)
    PyErr_Format(PyExc_KeyError, "missing option descriptor field '%s'", Name);
  return Value;
}

bool pythonString(PyObject *Value, std::string &Out, const char *Name,
                  bool AllowEmpty = true) {
  if (!PyUnicode_Check(Value)) {
    PyErr_Format(PyExc_TypeError, "%s must be a string", Name);
    return false;
  }
  Py_ssize_t Length = 0;
  const char *Data = PyUnicode_AsUTF8AndSize(Value, &Length);
  if (!Data)
    return false;
  StringRef Text(Data, static_cast<size_t>(Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0')) {
    PyErr_Format(PyExc_ValueError, "%s is empty or contains NUL", Name);
    return false;
  }
  Out = Text.str();
  return true;
}

bool dictionaryU32(PyObject *Dictionary, const char *Name, uint32_t &Out) {
  PyObject *Value = requiredDictionaryItem(Dictionary, Name);
  if (!Value || !PyLong_Check(Value) || PyBool_Check(Value)) {
    if (Value)
      PyErr_Format(PyExc_TypeError, "%s must be an integer", Name);
    return false;
  }
  unsigned long Result = PyLong_AsUnsignedLong(Value);
  if (PyErr_Occurred())
    return false;
  if (Result > std::numeric_limits<uint32_t>::max()) {
    PyErr_Format(PyExc_OverflowError, "%s does not fit uint32", Name);
    return false;
  }
  Out = static_cast<uint32_t>(Result);
  return true;
}

bool dictionaryBool(PyObject *Dictionary, const char *Name, NevercBool &Out) {
  PyObject *Value = requiredDictionaryItem(Dictionary, Name);
  if (!Value)
    return false;
  if (!PyBool_Check(Value)) {
    PyErr_Format(PyExc_TypeError, "%s must be bool", Name);
    return false;
  }
  Out = Value == Py_True ? NEVERC_TRUE : NEVERC_FALSE;
  return true;
}

bool dictionaryString(PyObject *Dictionary, const char *Name, std::string &Out,
                      bool AllowEmpty = true) {
  PyObject *Value = requiredDictionaryItem(Dictionary, Name);
  return Value && pythonString(Value, Out, Name, AllowEmpty);
}

bool dictionaryStringList(PyObject *Dictionary, const char *Name,
                          std::vector<std::string> &Out) {
  PyObject *Value = requiredDictionaryItem(Dictionary, Name);
  if (!Value)
    return false;
  PyRef Sequence(PySequence_Fast(Value, "option field must be a sequence"));
  if (!Sequence)
    return false;
  Py_ssize_t Count = PySequence_Fast_GET_SIZE(Sequence.get());
  if (Count < 0 || Count > 1024) {
    PyErr_Format(PyExc_ValueError, "%s has too many entries", Name);
    return false;
  }
  Out.reserve(static_cast<size_t>(Count));
  for (Py_ssize_t Index = 0; Index != Count; ++Index) {
    std::string Item;
    if (!pythonString(PySequence_Fast_GET_ITEM(Sequence.get(), Index), Item,
                      Name, false))
      return false;
    Out.push_back(std::move(Item));
  }
  return true;
}

NevercStringView view(StringRef Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

std::vector<NevercStringView> views(const std::vector<std::string> &Strings) {
  std::vector<NevercStringView> Result;
  Result.reserve(Strings.size());
  for (const std::string &String : Strings)
    Result.push_back(view(String));
  return Result;
}

PyObject *registerOption(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  PyObject *Dictionary = nullptr;
  if (!PyArg_ParseTuple(Arguments, "OO:register_option", &Handle, &Dictionary))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (Context->Kind != ContextKind::Registration || !Context->Registrar ||
      !Context->Registrar->RegisterOption || !Context->RegistrarContext) {
    PyErr_SetString(PyExc_RuntimeError,
                    "register_option requires an active registration context");
    return nullptr;
  }
  if (!PyDict_Check(Dictionary)) {
    PyErr_SetString(PyExc_TypeError, "option descriptor must be a dict");
    return nullptr;
  }

  std::string Spelling;
  std::string Help;
  std::string Metavar;
  std::vector<std::string> Aliases;
  std::vector<std::string> Conflicts;
  std::vector<std::string> Requires;
  NevercOptionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
                       NEVERC_DRIVER_API_MINOR, 0};
  if (!dictionaryString(Dictionary, "spelling", Spelling, false) ||
      !dictionaryU32(Dictionary, "form", Descriptor.Form) ||
      !dictionaryU32(Dictionary, "value_type", Descriptor.ValueType) ||
      !dictionaryU32(Dictionary, "multiplicity", Descriptor.Multiplicity) ||
      !dictionaryU32(Dictionary, "argument_count", Descriptor.ArgumentCount) ||
      !dictionaryBool(Dictionary, "required", Descriptor.Required) ||
      !dictionaryBool(Dictionary, "hidden", Descriptor.Hidden) ||
      !dictionaryString(Dictionary, "help", Help) ||
      !dictionaryString(Dictionary, "metavar", Metavar) ||
      !dictionaryStringList(Dictionary, "aliases", Aliases) ||
      !dictionaryStringList(Dictionary, "conflicts", Conflicts) ||
      !dictionaryStringList(Dictionary, "requires", Requires))
    return nullptr;

  std::vector<std::string> EnumNames;
  std::vector<int64_t> EnumNumbers;
  PyObject *EnumObject = requiredDictionaryItem(Dictionary, "enum_values");
  if (!EnumObject)
    return nullptr;
  PyRef EnumSequence(
      PySequence_Fast(EnumObject, "enum_values must be a sequence"));
  if (!EnumSequence)
    return nullptr;
  Py_ssize_t EnumCount = PySequence_Fast_GET_SIZE(EnumSequence.get());
  if (EnumCount < 0 || EnumCount > 1024) {
    PyErr_SetString(PyExc_ValueError, "enum_values has too many entries");
    return nullptr;
  }
  EnumNames.reserve(static_cast<size_t>(EnumCount));
  EnumNumbers.reserve(static_cast<size_t>(EnumCount));
  for (Py_ssize_t Index = 0; Index != EnumCount; ++Index) {
    PyObject *Pair = PySequence_Fast_GET_ITEM(EnumSequence.get(), Index);
    PyRef Items(PySequence_Fast(Pair, "enum value must be a pair"));
    if (!Items || PySequence_Fast_GET_SIZE(Items.get()) != 2) {
      if (!PyErr_Occurred())
        PyErr_SetString(PyExc_ValueError, "enum value must be a pair");
      return nullptr;
    }
    std::string Name;
    if (!pythonString(PySequence_Fast_GET_ITEM(Items.get(), 0), Name,
                      "enum name", false))
      return nullptr;
    PyObject *Number = PySequence_Fast_GET_ITEM(Items.get(), 1);
    if (!PyLong_Check(Number) || PyBool_Check(Number)) {
      PyErr_SetString(PyExc_TypeError, "enum value must be an integer");
      return nullptr;
    }
    long long NativeNumber = PyLong_AsLongLong(Number);
    if (PyErr_Occurred())
      return nullptr;
    EnumNames.push_back(std::move(Name));
    EnumNumbers.push_back(static_cast<int64_t>(NativeNumber));
  }

  std::vector<NevercStringView> AliasViews = views(Aliases);
  std::vector<NevercStringView> ConflictViews = views(Conflicts);
  std::vector<NevercStringView> RequireViews = views(Requires);
  std::vector<NevercOptionEnumValue> EnumValues;
  EnumValues.reserve(EnumNames.size());
  for (size_t Index = 0; Index != EnumNames.size(); ++Index) {
    NevercOptionEnumValue Value{};
    Value.Header = {sizeof(Value), NEVERC_DRIVER_API_MAJOR,
                    NEVERC_DRIVER_API_MINOR, 0};
    Value.Name = view(EnumNames[Index]);
    Value.Value = EnumNumbers[Index];
    EnumValues.push_back(Value);
  }

  Descriptor.Spelling = view(Spelling);
  Descriptor.Aliases = {AliasViews.data(), AliasViews.size(),
                        sizeof(NevercStringView)};
  Descriptor.Help = view(Help);
  Descriptor.Metavar = view(Metavar);
  Descriptor.EnumValues = {EnumValues.data(), EnumValues.size(),
                           sizeof(NevercOptionEnumValue)};
  Descriptor.Conflicts = {ConflictViews.data(), ConflictViews.size(),
                          sizeof(NevercStringView)};
  Descriptor.Requires = {RequireViews.data(), RequireViews.size(),
                         sizeof(NevercStringView)};
  NevercStatus Result = Context->Registrar->RegisterOption(
      Context->RegistrarContext, &Descriptor);
  if (Result.Code != NEVERC_STATUS_OK) {
    PyErr_Format(PyExc_ValueError,
                 "NeverC rejected the option descriptor (status %d)",
                 Result.Code);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject *registerObserver(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  unsigned long long High = 0;
  unsigned long long Low = 0;
  const char *PhaseData = nullptr;
  Py_ssize_t PhaseLength = 0;
  unsigned long Points = 0;
  PyObject *Callback = nullptr;
  if (!PyArg_ParseTuple(Arguments, "OKKs#kO:register_observer", &Handle, &High,
                        &Low, &PhaseData, &PhaseLength, &Points, &Callback))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (Context->Kind != ContextKind::Registration || !Context->Registrar ||
      !Context->Registrar->RegisterObserver || !Context->RegistrarContext) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "register_observer requires an active registration context");
    return nullptr;
  }
  if (!PyCallable_Check(Callback)) {
    PyErr_SetString(PyExc_TypeError, "observer callback must be callable");
    return nullptr;
  }
  if (Points > std::numeric_limits<uint32_t>::max()) {
    PyErr_SetString(PyExc_OverflowError, "observer points do not fit uint32");
    return nullptr;
  }

  auto Binding = std::make_unique<ObserverBinding>();
  Binding->Token = Context->Token;
  Py_INCREF(Callback);
  Binding->Callback = Callback;
  Binding->PhaseName.assign(PhaseData, static_cast<size_t>(PhaseLength));

  NevercObserverDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = {static_cast<uint64_t>(High), static_cast<uint64_t>(Low)};
  Descriptor.Points = static_cast<NevercObserverPoint>(Points);
  Descriptor.Callback = observerCallback;
  Descriptor.UserData = Binding.get();
  Descriptor.DestroyUserData = destroyObserverBinding;
  NevercStatus Result = Context->Registrar->RegisterObserver(
      Context->RegistrarContext, &Descriptor);
  if (Result.Code != NEVERC_STATUS_OK) {
    PyErr_Format(PyExc_ValueError,
                 "NeverC rejected the observer descriptor (status %d)",
                 Result.Code);
    return nullptr;
  }
  (void)Binding.release();
  Py_RETURN_NONE;
}

PyObject *optionValues(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  const char *SpellingData = nullptr;
  Py_ssize_t SpellingLength = 0;
  if (!PyArg_ParseTuple(Arguments, "Os#:option_values", &Handle, &SpellingData,
                        &SpellingLength))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (!isKind(*Context,
              {ContextKind::Session, ContextKind::Task, ContextKind::Frame}) ||
      !Context->Core || !Context->Core->GetPluginOptionValueCount) {
    PyErr_SetString(PyExc_RuntimeError,
                    "option_values requires an active session callback");
    return nullptr;
  }
  StringRef PluginID = Context->Token->Runtime->pluginID();
  NevercStringView PluginIDView = view(PluginID);
  NevercStringView Spelling{SpellingData,
                            static_cast<uint64_t>(SpellingLength)};
  uint64_t Count = 0;
  NevercStatus Result = Context->Core->GetPluginOptionValueCount(
      Context->Core->Context, Context->Session, PluginIDView, Spelling, &Count);
  if (Result.Code == NEVERC_STATUS_NOT_FOUND)
    return PyTuple_New(0);
  if (Result.Code != NEVERC_STATUS_OK ||
      Count > static_cast<uint64_t>(PY_SSIZE_T_MAX)) {
    PyErr_Format(PyExc_RuntimeError, "NeverC option lookup failed (status %d)",
                 Result.Code);
    return nullptr;
  }
  PyObject *Tuple = PyTuple_New(static_cast<Py_ssize_t>(Count));
  if (!Tuple)
    return nullptr;
  for (uint64_t Index = 0; Index != Count; ++Index) {
    NevercStringView Value{};
    Result = Context->Core->GetPluginOptionValue(Context->Core->Context,
                                                 Context->Session, PluginIDView,
                                                 Spelling, Index, &Value);
    PyObject *Item =
        Result.Code == NEVERC_STATUS_OK ? stringViewObject(Value) : nullptr;
    if (!Item) {
      Py_DECREF(Tuple);
      if (!PyErr_Occurred())
        PyErr_Format(PyExc_RuntimeError,
                     "NeverC option value lookup failed (status %d)",
                     Result.Code);
      return nullptr;
    }
    PyTuple_SET_ITEM(Tuple, static_cast<Py_ssize_t>(Index), Item);
  }
  return Tuple;
}

PyObject *checkCancelled(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  if (!PyArg_ParseTuple(Arguments, "O:check_cancelled", &Handle))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (!isKind(*Context, {ContextKind::Task, ContextKind::Frame}) ||
      !Context->Core || !Context->Core->CheckCancelled) {
    PyErr_SetString(PyExc_RuntimeError,
                    "check_cancelled requires an active task callback");
    return nullptr;
  }
  NevercStatus Result =
      Context->Core->CheckCancelled(Context->Core->Context, Context->Task);
  if (Result.Code == NEVERC_STATUS_OK)
    Py_RETURN_NONE;
  if (Result.Code == NEVERC_STATUS_CANCELLED) {
    PyErr_SetString(PyExc_RuntimeError, "NeverC task is cancelled");
    return nullptr;
  }
  PyErr_Format(PyExc_RuntimeError,
               "NeverC cancellation check failed (status %d)", Result.Code);
  return nullptr;
}

PyObject *emitDiagnostic(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  unsigned long Severity = 0;
  const char *MessageData = nullptr;
  Py_ssize_t MessageLength = 0;
  unsigned long Code = 0;
  if (!PyArg_ParseTuple(Arguments, "Oks#k:emit_diagnostic", &Handle, &Severity,
                        &MessageData, &MessageLength, &Code))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (!isKind(*Context,
              {ContextKind::Session, ContextKind::Task, ContextKind::Frame}) ||
      !Context->Core || !Context->Core->EmitDiagnostic) {
    PyErr_SetString(PyExc_RuntimeError,
                    "diagnostics require an active session callback");
    return nullptr;
  }
  if (Severity > NEVERC_DIAGNOSTIC_FATAL || Code > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "invalid diagnostic severity or code");
    return nullptr;
  }
  StringRef PluginID = Context->Token->Runtime->pluginID();
  NevercDiagnosticDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_CORE_API_MAJOR,
                       NEVERC_CORE_API_MINOR, 0};
  Descriptor.Severity = static_cast<NevercDiagnosticSeverity>(Severity);
  Descriptor.Code = static_cast<uint32_t>(Code);
  Descriptor.PluginID = view(PluginID);
  Descriptor.PhaseID = view(Context->PhaseName);
  Descriptor.Message = {MessageData, static_cast<uint64_t>(MessageLength)};
  NevercDiagnosticHandle Diagnostic{};
  NevercStatus Result = Context->Core->EmitDiagnostic(Context->Core->Context,
                                                      &Descriptor, &Diagnostic);
  if (Result.Code != NEVERC_STATUS_OK) {
    PyErr_Format(PyExc_RuntimeError,
                 "NeverC diagnostic emission failed (status %d)", Result.Code);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject *frameArguments(PyObject *, PyObject *Arguments) {
  PyObject *Handle = nullptr;
  if (!PyArg_ParseTuple(Arguments, "O:frame_arguments", &Handle))
    return nullptr;
  NativeContext *Context = checkedContext(Handle);
  if (!Context)
    return nullptr;
  if (Context->Kind != ContextKind::Frame || !Context->Frame ||
      !Context->Core || !Context->Core->QueryInterface) {
    PyErr_SetString(PyExc_RuntimeError,
                    "frame_arguments requires an active observer frame");
    return nullptr;
  }

  const void *Table = nullptr;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercInterfaceID DriverID{NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW};
  NevercStatus Result = Context->Core->QueryInterface(
      Context->Core->Context, DriverID, NEVERC_DRIVER_API_MAJOR,
      /*MinimumMinor=*/0, &Table, &Minor, &StructSize);
  constexpr uint64_t Required = offsetof(NevercDriverAPI, GetArgument) +
                                sizeof(NevercDriverAPI::GetArgument);
  if (Result.Code != NEVERC_STATUS_OK || !Table || StructSize < Required) {
    PyErr_Format(PyExc_RuntimeError,
                 "NeverC driver interface is unavailable (status %d)",
                 Result.Code);
    return nullptr;
  }
  (void)Minor;
  const auto *Driver = static_cast<const NevercDriverAPI *>(Table);
  uint64_t Count = 0;
  Result = Driver->GetArgumentCount(Driver->Context, Context->Frame,
                                    Context->Frame->Input, &Count);
  if (Result.Code != NEVERC_STATUS_OK ||
      Count > static_cast<uint64_t>(PY_SSIZE_T_MAX)) {
    PyErr_Format(PyExc_RuntimeError,
                 "NeverC raw argument lookup failed (status %d)", Result.Code);
    return nullptr;
  }
  PyObject *Tuple = PyTuple_New(static_cast<Py_ssize_t>(Count));
  if (!Tuple)
    return nullptr;
  for (uint64_t Index = 0; Index != Count; ++Index) {
    NevercStringView Value{};
    NevercStringView Source{};
    NevercArgumentOrigin Origin = NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE;
    uint64_t Position = 0;
    Result = Driver->GetArgument(Driver->Context, Context->Frame,
                                 Context->Frame->Input, Index, &Value, &Origin,
                                 &Source, &Position);
    PyRef ValueObject(Result.Code == NEVERC_STATUS_OK ? stringViewObject(Value)
                                                      : nullptr);
    PyRef OriginObject(PyLong_FromUnsignedLong(Origin));
    PyRef SourceObject(
        Result.Code == NEVERC_STATUS_OK ? stringViewObject(Source) : nullptr);
    PyRef PositionObject(PyLong_FromUnsignedLongLong(Position));
    if (!ValueObject || !OriginObject || !SourceObject || !PositionObject) {
      Py_DECREF(Tuple);
      if (!PyErr_Occurred())
        PyErr_Format(PyExc_RuntimeError,
                     "NeverC raw argument lookup failed (status %d)",
                     Result.Code);
      return nullptr;
    }
    PyObject *Item = PyTuple_New(4);
    if (!Item) {
      Py_DECREF(Tuple);
      return nullptr;
    }
    PyTuple_SET_ITEM(Item, 0, ValueObject.release());
    PyTuple_SET_ITEM(Item, 1, OriginObject.release());
    PyTuple_SET_ITEM(Item, 2, SourceObject.release());
    PyTuple_SET_ITEM(Item, 3, PositionObject.release());
    PyTuple_SET_ITEM(Tuple, static_cast<Py_ssize_t>(Index), Item);
  }
  return Tuple;
}

Expected<std::string> unicodeAttribute(PyObject *Object, const char *Name,
                                       StringRef Description,
                                       bool AllowEmpty = false) {
  PyRef Attribute(PyObject_GetAttrString(Object, Name));
  if (!Attribute)
    return pythonPluginError("missing " + Description + ": " +
                             formatPythonException());
  std::string Result;
  if (!pythonString(Attribute.get(), Result, Description.str().c_str(),
                    AllowEmpty))
    return pythonPluginError("invalid " + Description + ": " +
                             formatPythonException());
  if (!json::isUTF8(Result))
    return pythonPluginError(Description + " is not valid UTF-8");
  return Result;
}

Error validateSemVerIdentifiers(StringRef Value, bool IsPrerelease,
                                StringRef Description) {
  if (Value.empty())
    return Error::success();
  while (!Value.empty()) {
    auto Part = Value.split('.');
    StringRef Identifier = Part.first;
    if (Identifier.empty())
      return pythonPluginError(Description + " contains an empty identifier");
    bool Numeric = true;
    for (char Character : Identifier) {
      bool Valid = (Character >= '0' && Character <= '9') ||
                   (Character >= 'A' && Character <= 'Z') ||
                   (Character >= 'a' && Character <= 'z') || Character == '-';
      if (!Valid)
        return pythonPluginError(Description +
                                 " contains a non-SemVer character");
      Numeric &= Character >= '0' && Character <= '9';
    }
    if (IsPrerelease && Numeric && Identifier.size() > 1 &&
        Identifier.front() == '0')
      return pythonPluginError(Description +
                               " has a numeric identifier with a leading zero");
    Value = Part.second;
  }
  return Error::success();
}

Expected<PyObject *> importScript(StringRef CanonicalPath,
                                  std::string &ModuleName) {
  PyObject *Modules = PyImport_GetModuleDict();
  PyObject *Bridge =
      Modules ? PyDict_GetItemString(Modules, BridgeModuleName) : nullptr;
  auto *State =
      Bridge ? static_cast<BridgeState *>(PyModule_GetState(Bridge)) : nullptr;
  if (!State) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NeverC's Python bridge has no module state");
    return pythonPluginError("cannot allocate a Python plugin module ID: " +
                             formatPythonException());
  }
  uint64_t ID = State->NextModuleID;
  if (ID == std::numeric_limits<uint64_t>::max()) {
    PyErr_SetString(PyExc_OverflowError,
                    "Python plugin module IDs are exhausted");
    return pythonPluginError("cannot allocate a Python plugin module ID: " +
                             formatPythonException());
  }
  State->NextModuleID = ID + 1;
  ModuleName = (Twine("_neverc_plugin_script_") + Twine(ID)).str();

  PyRef Importlib(PyImport_ImportModule("importlib.util"));
  PyRef SpecFactory(Importlib ? PyObject_GetAttrString(
                                    Importlib.get(), "spec_from_file_location")
                              : nullptr);
  PyRef ModuleFactory(
      Importlib ? PyObject_GetAttrString(Importlib.get(), "module_from_spec")
                : nullptr);
  PyRef Name(PyUnicode_FromStringAndSize(
      ModuleName.data(), static_cast<Py_ssize_t>(ModuleName.size())));
  PyRef Path(PyUnicode_DecodeFSDefaultAndSize(
      CanonicalPath.data(), static_cast<Py_ssize_t>(CanonicalPath.size())));
  if (!Importlib || !SpecFactory || !ModuleFactory || !Name || !Path)
    return pythonPluginError("cannot prepare Python plugin import: " +
                             formatPythonException());
  PyRef Spec(PyObject_CallFunctionObjArgs(SpecFactory.get(), Name.get(),
                                          Path.get(), nullptr));
  PyRef Module(Spec ? PyObject_CallFunctionObjArgs(ModuleFactory.get(),
                                                   Spec.get(), nullptr)
                    : nullptr);
  if (!Spec || !Module)
    return pythonPluginError("cannot create Python plugin module: " +
                             formatPythonException());

  if (!Modules ||
      PyDict_SetItemString(Modules, ModuleName.c_str(), Module.get()) < 0)
    return pythonPluginError("cannot publish Python plugin module: " +
                             formatPythonException());
  auto RemovePartial = make_scope_exit([&] {
    if (PyDict_DelItemString(Modules, ModuleName.c_str()) < 0)
      PyErr_Clear();
  });

  SmallString<256> Parent(CanonicalPath);
  sys::path::remove_filename(Parent);
  PyObject *SearchPath = PySys_GetObject("path"); // Borrowed.
  PyRef ParentObject(PyUnicode_DecodeFSDefaultAndSize(
      Parent.data(), static_cast<Py_ssize_t>(Parent.size())));
  bool AddedParent = SearchPath && ParentObject &&
                     PyList_Insert(SearchPath, 0, ParentObject.get()) == 0;
  if (!AddedParent) {
    PyErr_Clear();
    return pythonPluginError(
        "cannot add the Python plugin directory to sys.path");
  }
  auto RemoveParent = make_scope_exit([&] {
    Py_ssize_t Count = PyList_Size(SearchPath);
    for (Py_ssize_t Index = 0; Index < Count; ++Index) {
      if (PyList_GET_ITEM(SearchPath, Index) != ParentObject.get())
        continue;
      if (PySequence_DelItem(SearchPath, Index) < 0)
        PyErr_Clear();
      break;
    }
  });

  PyRef Loader(PyObject_GetAttrString(Spec.get(), "loader"));
  PyRef Executor(Loader ? PyObject_GetAttrString(Loader.get(), "exec_module")
                        : nullptr);
  PyRef Executed(Executor ? PyObject_CallFunctionObjArgs(Executor.get(),
                                                         Module.get(), nullptr)
                          : nullptr);
  if (!Loader || !Executor || !Executed)
    return pythonPluginError("cannot import Python plugin '" + CanonicalPath +
                             "':\n" + formatPythonException());
  RemovePartial.release();
  return Module.release();
}

Expected<PythonPluginLoadResult> loadPythonPluginImpl(StringRef CanonicalPath) {
  addAdjacentSDKPath();
  std::string ModuleName;
  auto Imported = importScript(CanonicalPath, ModuleName);
  if (!Imported)
    return Imported.takeError();
  PyRef Module(*Imported);
  PyObject *Modules = PyImport_GetModuleDict();
  auto RemoveModule = make_scope_exit([&] {
    if (Modules && PyDict_DelItemString(Modules, ModuleName.c_str()) < 0)
      PyErr_Clear();
  });

  PyRef Spec(PyObject_GetAttrString(Module.get(), "__neverc_plugin__"));
  if (!Spec)
    return pythonPluginError(
        "Python plugin does not declare exactly one @Plugin class: " +
        formatPythonException());
  auto PluginID = unicodeAttribute(Spec.get(), "id", "plugin id");
  if (!PluginID)
    return PluginID.takeError();
  if (!isCanonicalPluginID(*PluginID))
    return pythonPluginError("Python plugin ID is not canonical");
  auto DisplayName = unicodeAttribute(Spec.get(), "name", "plugin name");
  if (!DisplayName)
    return DisplayName.takeError();
  auto Prerelease =
      unicodeAttribute(Spec.get(), "prerelease", "plugin prerelease", true);
  if (!Prerelease)
    return Prerelease.takeError();
  auto BuildMetadata = unicodeAttribute(Spec.get(), "build_metadata",
                                        "plugin build metadata", true);
  if (!BuildMetadata)
    return BuildMetadata.takeError();
  if (Error E =
          validateSemVerIdentifiers(*Prerelease, true, "plugin prerelease"))
    return std::move(E);
  if (Error E = validateSemVerIdentifiers(*BuildMetadata, false,
                                          "plugin build metadata"))
    return std::move(E);

  PyRef VersionInfo(PyObject_GetAttrString(Spec.get(), "version_info"));
  PyRef VersionSequence(
      VersionInfo
          ? PySequence_Fast(VersionInfo.get(),
                            "plugin version_info must be a 3-item sequence")
          : nullptr);
  if (!VersionSequence || PySequence_Fast_GET_SIZE(VersionSequence.get()) != 3)
    return pythonPluginError("invalid plugin version_info: " +
                             formatPythonException());
  uint32_t VersionComponents[3]{};
  for (size_t Index = 0; Index != 3; ++Index) {
    PyObject *Value = PySequence_Fast_GET_ITEM(VersionSequence.get(),
                                               static_cast<Py_ssize_t>(Index));
    if (!PyLong_Check(Value) || PyBool_Check(Value))
      return pythonPluginError(
          "plugin version_info components must be integers");
    unsigned long Component = PyLong_AsUnsignedLong(Value);
    if (PyErr_Occurred() || Component > UINT32_MAX) {
      std::string Detail = formatPythonException();
      return pythonPluginError("plugin version component exceeds uint32: " +
                               Detail);
    }
    VersionComponents[Index] = static_cast<uint32_t>(Component);
  }

  PyRef PluginClass(PyObject_GetAttrString(Spec.get(), "plugin_class"));
  if (!PluginClass)
    return pythonPluginError("missing plugin class: " +
                             formatPythonException());
  if (!PyType_Check(PluginClass.get()) || !PyCallable_Check(PluginClass.get()))
    return pythonPluginError("Python plugin_class is not a callable class");
  auto hasHook = [&](const char *Name) -> Expected<bool> {
    int Present = PyObject_HasAttrString(PluginClass.get(), Name);
    if (Present < 0)
      return pythonPluginError("cannot inspect Python hook '" + Twine(Name) +
                               "': " + formatPythonException());
    return Present == 1;
  };
  auto HasSessionBegin = hasHook("on_session_begin");
  if (!HasSessionBegin)
    return HasSessionBegin.takeError();
  auto HasSessionEnd = hasHook("on_session_end");
  if (!HasSessionEnd)
    return HasSessionEnd.takeError();
  auto HasTaskBegin = hasHook("on_task_begin");
  if (!HasTaskBegin)
    return HasTaskBegin.takeError();
  auto HasTaskEnd = hasHook("on_task_end");
  if (!HasTaskEnd)
    return HasTaskEnd.takeError();

  PyRef APIModule(PyImport_ImportModule("neverc_plugin.api"));
  if (!APIModule)
    return pythonPluginError("cannot import the NeverC Python SDK: " +
                             formatPythonException());

  PluginDescriptorRecord Descriptor;
  Descriptor.ABIMajor = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.ABIMinor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = *PluginID;
  Descriptor.DisplayName = *DisplayName;
  Descriptor.Version.Major = VersionComponents[0];
  Descriptor.Version.Minor = VersionComponents[1];
  Descriptor.Version.Patch = VersionComponents[2];
  Descriptor.VersionPrerelease = *Prerelease;
  Descriptor.VersionBuildMetadata = *BuildMetadata;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;

  auto Runtime = std::make_unique<PythonPluginRuntime>(
      ModuleName, Module.release(), PluginClass.release(), APIModule.release(),
      *PluginID, *HasSessionBegin || *HasSessionEnd,
      *HasTaskBegin || *HasTaskEnd);
  RemoveModule.release();
  PythonPluginLoadResult Result;
  Result.Descriptor = std::move(Descriptor);
  Result.Runtime = std::move(Runtime);
  return Result;
}

} // namespace

Expected<PythonPluginLoadResult> loadPythonPlugin(StringRef CanonicalPath) {
  std::string InterpreterError = initializeInterpreter();
  if (!InterpreterError.empty())
    return pythonPluginError("cannot initialize Python plugin runtime: " +
                             InterpreterError);
  if (!Py_IsInitialized())
    return pythonPluginError("CPython did not initialize");
  GILGuard Guard;
  if (!installBridgeModule())
    return pythonPluginError("cannot install Python plugin bridge: " +
                             formatPythonException());
  return loadPythonPluginImpl(CanonicalPath);
}

} // namespace neverc::plugin
