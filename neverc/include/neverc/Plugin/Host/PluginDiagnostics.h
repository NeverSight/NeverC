#ifndef NEVERC_PLUGIN_HOST_PLUGINDIAGNOSTICS_H
#define NEVERC_PLUGIN_HOST_PLUGINDIAGNOSTICS_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginSession;
class PluginTaskContext;

struct PluginDiagnosticRecord {
  NevercDiagnosticSeverity Severity = NEVERC_DIAGNOSTIC_ERROR;
  uint32_t Code = 0;
  std::string PluginID;
  std::string PhaseID;
  std::string Message;
  std::vector<std::string> Notes;
  uint64_t TaskOrder = 0;
  uint64_t PhaseOrder = 0;
  uint64_t PluginOrder = 0;
  uint64_t Sequence = 0;
  uint64_t DetailToken = 0;
  uint64_t TransactionID = 0;
  bool Implicit = false;
};

class PluginDiagnostics {
public:
  uint64_t beginTransaction();
  void discardTransaction(uint64_t TransactionID);

  NevercStatus emit(PluginSession &Session, PluginTaskContext *Task,
                    llvm::StringRef ActivePluginID,
                    llvm::StringRef CallbackName,
                    uint64_t TransactionID,
                    const NevercDiagnosticDescriptor &Descriptor,
                    NevercDiagnosticHandle &OutDiagnostic);

  void emitImplicit(PluginSession &Session, PluginTaskContext *Task,
                    llvm::StringRef PluginID, llvm::StringRef PhaseID,
                    llvm::StringRef Message, uint64_t TransactionID);

  bool ownsDetail(uint64_t DetailToken,
                  llvm::StringRef PluginID,
                  uint64_t TransactionID) const;
  std::string messageForDetail(uint64_t DetailToken) const;
  std::vector<PluginDiagnosticRecord> takeSorted();

private:
  uint64_t append(PluginSession &Session, PluginTaskContext *Task,
                  NevercDiagnosticSeverity Severity, uint32_t Code,
                  llvm::StringRef PluginID, llvm::StringRef PhaseID,
                  llvm::StringRef Message,
                  std::vector<std::string> Notes, uint64_t TransactionID,
                  bool Implicit);

  mutable std::mutex Mutex;
  std::vector<PluginDiagnosticRecord> Records;
  uint64_t NextSequence = 1;
  uint64_t NextTransactionID = 1;
};

} // namespace neverc::plugin

#endif
