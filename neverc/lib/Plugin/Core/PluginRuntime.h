#ifndef NEVERC_LIB_PLUGIN_CORE_PLUGINRUNTIME_H
#define NEVERC_LIB_PLUGIN_CORE_PLUGINRUNTIME_H

#include "neverc/Plugin/PluginCore.h"
#include <string>

namespace neverc::plugin {

/// Host-internal lifecycle adapter for non-native plugin implementations.
///
/// Native plugins continue to dispatch directly through the public C
/// descriptor.  A runtime-backed module uses this interface so the host can
/// retain per-module identity across callbacks such as ProcessBegin that carry
/// no descriptor or user-data argument.  This is deliberately private to the
/// host and does not change the public plugin ABI.
class PluginRuntime {
public:
  virtual ~PluginRuntime() = default;

  virtual bool hasProcessBegin() const = 0;
  virtual bool hasRegister() const = 0;
  virtual bool hasSessionBegin() const = 0;
  virtual bool hasSessionEnd() const = 0;
  virtual bool hasTaskBegin() const = 0;
  virtual bool hasTaskEnd() const = 0;
  virtual bool hasDestroy() const = 0;

  virtual NevercStatus processBegin(const NevercCoreAPI *Core,
                                    void **OutProcessState) = 0;
  virtual NevercStatus registerPlugin(const NevercCoreAPI *Core,
                                      const NevercRegistrarAPI *Registrar,
                                      void *RegistrarContext,
                                      void *ProcessState) = 0;
  virtual NevercStatus sessionBegin(const NevercCoreAPI *Core,
                                    NevercSessionHandle Session,
                                    void *ProcessState,
                                    void **OutSessionState) = 0;
  virtual NevercStatus sessionEnd(const NevercCoreAPI *Core,
                                  NevercSessionHandle Session,
                                  void *ProcessState, void *SessionState) = 0;
  virtual NevercStatus taskBegin(const NevercCoreAPI *Core,
                                 NevercTaskHandle Task, NevercTaskKind Kind,
                                 void *ProcessState, void *SessionState,
                                 void **OutTaskState) = 0;
  virtual NevercStatus taskEnd(const NevercCoreAPI *Core, NevercTaskHandle Task,
                               NevercTaskKind Kind, void *ProcessState,
                               void *SessionState, void *TaskState) = 0;
  virtual NevercStatus destroy(const NevercCoreAPI *Core,
                               void *ProcessState) = 0;

  /// Returns additional language-runtime context for the most recent failed
  /// lifecycle callback.  Native plugins have no corresponding detail string.
  virtual std::string lastError() const { return {}; }
};

} // namespace neverc::plugin

#endif
